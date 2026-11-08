/* Offline replay of a BB_PAD_LOG=1 log through the stick calibration (src/runtime_pad.c), triage round 8.
 *
 *   replay_pad_log <log> [memory 0|1] [settle_ms] [warm 0|1]
 *
 * The log has one "PAD: raw ..." line per second, so each logged raw value is held for one second at 60 Hz;
 * when it differs from the previous one by more than REST_SPREAD, the first settle_ms of that second are a
 * straight ramp from the previous value (settle_ms 0: an instant step, the best case for any logic). memory 0
 * is the round-5 logic (stuck capture + 3 s auto-release), memory 1 the round-8 logic. warm 1 runs the log
 * twice and measures the second run with the rest points learned in the first (the persistence file).
 * Prints one summary line to stderr; the calibration's own log lines go to stdout.
 *
 * "Rest state" (the owner's, docs/macos/M5-playable.md, owner session 4): A = true centre, |rx|,|ry| < 3000;
 * B = stuck, ry in [-23000,-17000] and rx in [-3000,5000]. Off-centre = the corrected, dead-zoned right stick
 * sends anything but (128,128) while the raw value is in A or B. */
#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
_Thread_local int runtime_ndgi_open;
#include "../src/runtime_pad.c"

int bbgpu_key_f9_take(void) { return 0; }
int bbgpu_overlay_captures_input(void) { return 0; }
void bbgpu_mouse_configure(int look) { (void)look; }
int bbgpu_mouse_sample(float *dx, float *dy, uint32_t *buttons, int *wheel) { *dx=*dy=0; *buttons=0; *wheel=0; return 0; }
int bbgpu_camera_yaw(float *yaw_degrees, double *age_seconds) { (void)yaw_degrees; (void)age_seconds; return 0; }
void bbgpu_prompts_controller(int device) { (void)device; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) { (void)table; (void)count; (void)name; return 0; }

typedef struct { int raw[4]; int cx, cy; } Sample;
static int cls(const Sample *s) {
    const int x=s->raw[2], y=s->raw[3];
    if (SDL_abs(x)<3000 && SDL_abs(y)<3000) return 'A';
    if (y>=-23000 && y<=-17000 && x>=-3000 && x<=5000) return 'B';
    return 'M';
}

/* A logged second counts as a settled rest state when a neighbouring second is in the same state and within
 * 1500 on both axes (a stick sweeping through a band for one second is input, not a rest). */
static int stable(const Sample *smp, int n, int i) {
    for (int d=-1; d<=1; d+=2) {
        const int j=i+d;
        if (j<0 || j>=n || cls(&smp[j])!=cls(&smp[i])) continue;
        if (SDL_abs(smp[j].raw[2]-smp[i].raw[2])<=1500 && SDL_abs(smp[j].raw[3]-smp[i].raw[3])<=1500) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc<2) { fprintf(stderr,"usage: %s <log> [memory 0|1] [settle_ms] [warm 0|1]\n",argv[0]); return 2; }
    const int memory=argc>2 ? atoi(argv[2]) : 1, settle_ms=argc>3 ? atoi(argv[3]) : 0, warm=argc>4 ? atoi(argv[4]) : 0;
    setenv("BB_STICK_REST_MEMORY",memory ? "1" : "0",1);
    unsetenv("BB_PAD_LOG");
    FILE *f=fopen(argv[1],"r");
    if (!f) { perror(argv[1]); return 1; }
    static Sample smp[40000];
    int n=0;
    char line[1024];
    while (n<40000 && fgets(line,sizeof line,f)) {
        Sample *s=&smp[n];
        if (sscanf(line,"PAD: raw lx=%d ly=%d rx=%d ry=%d",&s->raw[0],&s->raw[1],&s->raw[2],&s->raw[3])==4) {
            const char *c=strstr(line,"centre r=(");
            if (!c || sscanf(c,"centre r=(%d,%d)",&s->cx,&s->cy)!=2) continue;
            ++n;
        }
    }
    fclose(f);
    const int dz2[2]={STICK_DEADZONE_LEFT,STICK_DEADZONE_RIGHT};
    PadCal c;
    cal_connect(&c,0); c.auto_state=1;
    const int runs=warm ? 2 : 1;
    double t_a=0, t_b=0, off_a=0, off_b=0, all_off=0, all_t=0; int centre_match=0, centre_cmp=0, captures=0, releases=0;
    int ep_open=0; double ep_len=0, ep_sum[5]={0}; int ep_n[5]={0}; /* episodes: <0.5 s, <1.5 s, <3.5 s, <10 s, longer */
    for (int run=0; run<runs; ++run) {
        const int measure=run==runs-1;
        PadCal keep=c;
        cal_connect(&c,0); c.auto_state=1;           /* a new session: centre back to (0,0) ... */
        if (run>0) { memcpy(c.rp_n,keep.rp_n,sizeof c.rp_n); memcpy(c.rp_x,keep.rp_x,sizeof c.rp_x);
                     memcpy(c.rp_y,keep.rp_y,sizeof c.rp_y); memcpy(c.rp_seq,keep.rp_seq,sizeof c.rp_seq); c.seq=keep.seq; }
        uint64_t t=1000000;
        for (int i=0;i<n;++i) {
            const Sample *s=&smp[i], *p=i ? &smp[i-1] : s;
            const int moved=SDL_abs(s->raw[2]-p->raw[2])>REST_SPREAD || SDL_abs(s->raw[3]-p->raw[3])>REST_SPREAD ||
                            SDL_abs(s->raw[0]-p->raw[0])>REST_SPREAD || SDL_abs(s->raw[1]-p->raw[1])>REST_SPREAD;
            const int steps=60, ramp=moved ? settle_ms*60/1000 : 0;
            for (int k=0;k<steps;++k,t+=16667) {
                int raw[4];
                for (int a=0;a<4;++a) raw[a]=k<ramp ? p->raw[a]+(s->raw[a]-p->raw[a])*(k+1)/(ramp+1) : s->raw[a];
                const int cx0=c.cx[1], cy0=c.cy[1];
                cal_update(&c,raw,dz2,t);
                if (measure && memory==0) { if ((cx0!=c.cx[1] || cy0!=c.cy[1]) && (c.cx[1] || c.cy[1])) ++captures;
                                            if ((cx0 || cy0) && !c.cx[1] && !c.cy[1]) ++releases; }
                if (measure && k==0 && settle_ms==0) {
                    ++centre_cmp;
                    if (SDL_abs(c.cx[1]-s->cx)<=300 && SDL_abs(c.cy[1]-s->cy)<=300) ++centre_match;
                }
                const int k_class=cls(s), st=stable(smp,n,i);
                if (measure && k_class!='M') {
                    int16_t qx, qy;
                    stick_calibrate((int16_t)raw[2],(int16_t)raw[3],c.cx[1],c.cy[1],&qx,&qy);
                    stick_deadzone(qx,qy,STICK_DEADZONE_RIGHT,&qx,&qy);
                    all_t+=1.0/60; all_off+=(axis(qx)!=128 || axis(qy)!=128)/60.0;
                }
                if (measure && k_class!='M' && st) {
                    int16_t ox, oy;
                    stick_calibrate((int16_t)raw[2],(int16_t)raw[3],c.cx[1],c.cy[1],&ox,&oy);
                    stick_deadzone(ox,oy,STICK_DEADZONE_RIGHT,&ox,&oy);
                    const int off=axis(ox)!=128 || axis(oy)!=128;
                    if (off) { ep_open=1; ep_len+=1.0/60; }
                    if (!off && ep_open) { const int b=ep_len<0.5?0:ep_len<1.5?1:ep_len<3.5?2:ep_len<10?3:4; ++ep_n[b]; ep_sum[b]+=ep_len; ep_open=0; ep_len=0; }
                    if (k_class=='A') { t_a+=1.0/60; off_a+=off/60.0; } else { t_b+=1.0/60; off_b+=off/60.0; }
                }
            }
        }
    }
    fprintf(stderr,"memory=%d settle=%dms warm=%d samples=%d | SETTLED rest A %.0f s (off-centre %.1f s) rest B %.0f s (off-centre %.1f s) "
            "| total off-centre in rest %.1f s | centre match vs log %d/%d%s\n",
            memory,settle_ms,warm,n,t_a,off_a,t_b,off_b,off_a+off_b,centre_match,centre_cmp,
            memory ? "" : " (round-5 logic)");
    fprintf(stderr,"  any second in a rest band (incl. real input sweeping through): %.0f s, off-centre %.1f s\n",all_t,all_off);
    fprintf(stderr,"  off-centre episodes (count/seconds): <0.5s %d/%.0f  <1.5s %d/%.0f  <3.5s %d/%.0f  <10s %d/%.0f  longer %d/%.0f\n",
            ep_n[0],ep_sum[0],ep_n[1],ep_sum[1],ep_n[2],ep_sum[2],ep_n[3],ep_sum[3],ep_n[4],ep_sum[4]);
    if (!memory) fprintf(stderr,"  round-5 centre changes: %d to a centre, %d releases\n",captures,releases);
    return 0;
}
