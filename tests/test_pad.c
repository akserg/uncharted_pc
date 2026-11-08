#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
_Thread_local int runtime_ndgi_open; /* normally defined in runtime_fiber.c; pad test does not link it */
#include "../src/runtime_pad.c"

static int capture;
static int rumble_calls;
static uint16_t rumble_low, rumble_high;
static int f9_request;
int bbgpu_key_f9_take(void) { const int r=f9_request; f9_request=0; return r; }
int bbgpu_overlay_captures_input(void) { return capture; }
void bbgpu_mouse_configure(int look) { (void)look; }
int bbgpu_mouse_sample(float *dx, float *dy, uint32_t *buttons, int *wheel) {
    *dx=*dy=0; *buttons=0; *wheel=0;
    return 0;
}
int bbgpu_camera_yaw(float *yaw_degrees, double *age_seconds) { (void)yaw_degrees; (void)age_seconds; return 0; }
void bbgpu_prompts_controller(int device) { (void)device; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static bool SDLCALL rumble(void *userdata, Uint16 low, Uint16 high) {
    (void)userdata;
    ++rumble_calls;
    rumble_low=low;
    rumble_high=high;
    return true;
}

static void set_env(const char *name, const char *value) {
#ifdef _WIN32
    assert(_putenv_s(name,value)==0);
#else
    assert(setenv(name,value,1)==0);
#endif
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}

/* Feeds sample-period steps (60 Hz) of the right stick at (rx,ry) +- jit for `ms`; returns the new time. Stops
 * early when stop_cy is given and the right centre reaches it (the time of that sample is *hit, else 0). */
#define SAMPLE_US 16667u
static uint64_t feed(PadCal *c, uint64_t t, int rx, int ry, int ms, int jit, uint64_t *hit, const int *until_cy) {
    static const int dz2[2]={7849,8689};
    if (hit) *hit=0;
    for (int i=0, n=(int)((uint64_t)ms*1000/SAMPLE_US); i<n; ++i, t+=SAMPLE_US) {
        const int raw[4]={0,-1,rx+((i%3)-1)*jit,ry+(((i/3)%3)-1)*jit};
        cal_update(c,raw,dz2,t);
        if (until_cy && !hit[0] && c->cy[1]==*until_cy) { hit[0]=t; }
    }
    return t;
}
/* A straight move of the right stick between two values at `ms` (spread far above REST_SPREAD). */
static uint64_t sweep(PadCal *c, uint64_t t, int x0, int y0, int x1, int y1, int ms) {
    static const int dz2[2]={7849,8689};
    const int n=(int)((uint64_t)ms*1000/SAMPLE_US);
    for (int i=0;i<n;++i,t+=SAMPLE_US) {
        const int raw[4]={0,-1,x0+(x1-x0)*i/n,y0+(y1-y0)*i/n};
        cal_update(c,raw,dz2,t);
    }
    return t;
}

/* The round-8 pipeline (centre, calibration, deadzone), the reference for the corridor tests. */
static void round8_output(const PadCal *c, int s, int x, int y, int dz, int16_t *ox, int16_t *oy) {
    stick_calibrate((int16_t)x,(int16_t)y,c->cx[s],c->cy[s],ox,oy);
    stick_deadzone(*ox,*oy,dz,ox,oy);
}
int main(void) {
    char restfile[]="bbport-rest-test-XXXXXX";
    { int rfd=mkstemp(restfile); assert(rfd>=0); close(rfd); unlink(restfile); }
    set_env("BB_PAD_REST_FILE",restfile);   /* tests never touch the player's user/pad_rest_points.txt */
    char path[]="bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    char config[]="bbport-input-test-XXXXXX";
    fd=mkstemp(config);
    assert(fd>=0);
    close(fd);
    inject(config,"pad_vibration=0\npad_vibration_strength=50\n");
    set_env("BB_PAD_FILE",path);
    set_env("BB_PAD_HOST","1");
    set_env("BB_INPUT_CONFIG",config);
    set_env("SDL_VIDEODRIVER","dummy");
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert(input.pad_vibration==0);
    assert(input.pad_vibration_strength==50);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    desc.Rumble=rumble;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    const uint8_t vibration[]={64,192};
    assert(pad_vibration(1,vibration)==0 && rumble_calls==0);
    input.pad_vibration=1;
    assert(pad_vibration(1,vibration)==0 && rumble_calls==1);
    assert(rumble_low==(64*257+1)/2 && rumble_high==(192*257+1)/2);
    input.pad_vibration_strength=100;
    assert(pad_vibration(1,vibration)==0 && rumble_calls==2);
    assert(rumble_low==64*257 && rumble_high==192*257);
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==0);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==1);
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    /* axis mapping: symmetric ends, exact centre */
    assert(axis(0)==128 && axis(-32768)==0 && axis(32767)==255);
    /* scaled radial deadzone */
    int16_t ox, oy;
    const int dz=8689;
    stick_deadzone(0,0,dz,&ox,&oy); assert(ox==0 && oy==0);
    stick_deadzone(2000,-3000,dz,&ox,&oy); assert(axis(ox)==128 && axis(oy)==128);        /* rest offset */
    stick_deadzone(-1500,6000,dz,&ox,&oy); assert(axis(ox)==128 && axis(oy)==128);
    stick_deadzone(8689,0,dz,&ox,&oy); assert(ox==0 && oy==0);                            /* on the edge */
    stick_deadzone(8690,0,dz,&ox,&oy); assert(ox>=0 && ox<=2);                            /* no jump outside */
    stick_deadzone(0,-8700,dz,&ox,&oy); assert(ox==0 && oy<=0 && oy>=-20);
    stick_deadzone(32767,0,dz,&ox,&oy); assert(axis(ox)==255 && axis(oy)==128);           /* full deflection */
    stick_deadzone(-32768,0,dz,&ox,&oy); assert(axis(ox)==0 && axis(oy)==128);
    stick_deadzone(0,32767,dz,&ox,&oy); assert(axis(oy)==255);
    stick_deadzone(0,-32768,dz,&ox,&oy); assert(axis(oy)==0);
    stick_deadzone(20000,10000,dz,&ox,&oy);                                               /* direction kept */
    assert(ox>oy && oy>0 && SDL_abs(ox-2*oy)<=2);
    stick_deadzone(-20000,15000,dz,&ox,&oy); assert(ox<0 && oy>0 && SDL_abs(3*ox+4*oy)<=8);
    stick_deadzone(3000,-9000,0,&ox,&oy); assert(ox==3000 && oy==-9000);                  /* 0 disables */
    for (int v=dz+1; v<32768; v+=97) {                                                    /* monotonic */
        int16_t a,b,c,e; stick_deadzone((int16_t)v,0,dz,&a,&b); stick_deadzone((int16_t)(v+1<32768?v+1:v),0,dz,&c,&e);
        assert(c>=a && b==0 && e==0);
    }
    /* rest-centre calibration */
    {
        /* centre (0,0) is the identity: byte-identical to round 2 */
        for (int v=-32768; v<32768; v+=7) assert(stick_calibrate_axis(v,0)==v);
        const int centres[2]={-23630,-20840};
        for (int ci=0; ci<2; ++ci) {
            const int c=centres[ci];
            assert(axis(stick_calibrate_axis(c,c))==128);
            assert(stick_calibrate_axis(-32768,c)==-32768 && axis(stick_calibrate_axis(-32768,c))==0);
            assert(stick_calibrate_axis(32767,c)==32767 && axis(stick_calibrate_axis(32767,c))==255);
            int prev=-40000;
            for (int v=-32768; v<32768; ++v) { const int o=stick_calibrate_axis(v,c); assert(o>=prev); prev=o; }
            /* through the deadzone: the faulty rest is exactly centre, a full push still reaches the ends */
            int16_t a, b;
            stick_calibrate(0,(int16_t)c,0,c,&a,&b); stick_deadzone(a,b,8689,&a,&b); assert(axis(a)==128 && axis(b)==128);
            stick_calibrate(0,32767,0,c,&a,&b); stick_deadzone(a,b,8689,&a,&b); assert(axis(b)==255);
            stick_calibrate(0,-32768,0,c,&a,&b); stick_deadzone(a,b,8689,&a,&b); assert(axis(b)==0);
        }
        const int dz2[2]={7849,8689};
        /* stuck-stick detection: 0 for 2 s, then a constant -21015 (+-30 jitter): captured after 3 s, not before */
        PadCal c; uint64_t t=1000000;
        cal_connect(&c,t); c.auto_state=1;
        int log_n=0;
        for (int i=0;i<120;++i,t+=16667) { int raw[4]={0,-1,0,-1}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cx[1]==0 && c.cy[1]==0);
        const uint64_t t0=t;
        for (int i=0;i<300;++i,t+=16667) {
            int raw[4]={0,-1,-530+(i%4)*10,-21015+(i%3)*20}; cal_stuck(&c,raw,dz2,t);
            if (t-t0<2900000) assert(c.cy[1]==0);                                /* not before 3 s */
            if (t-t0>=3000000+16667) { assert(c.cy[1]<=-20975 && c.cy[1]>=-21015 && SDL_abs(c.cx[1]+520)<=20); ++log_n; }
        }
        assert(log_n>0 && c.cx[0]==0 && c.cy[0]==0);                              /* left at rest: no centre */
        /* a human-like hold (+-300 jitter, deflected) is not captured, even after 10 s */
        cal_connect(&c,t); c.auto_state=1;
        for (int i=0;i<600;++i,t+=16667) { int raw[4]={0,0,(i&1)?300:-300,-21000+((i/2)&1)*600-300}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cx[1]==0 && c.cy[1]==0);
        /* a stick moving at 100 per 0.5 s never reaches 3 s of constancy */
        for (int i=0;i<600;++i,t+=16667) { int raw[4]={0,0,0,-15000-(i/30)*100}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cy[1]==0);
        /* a constant value inside the deadzone: no capture */
        cal_connect(&c,t); c.auto_state=1;
        for (int i=0;i<600;++i,t+=16667) { int raw[4]={0,0,100,-800}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cx[1]==0 && c.cy[1]==0);
        /* a constant value beyond 85 %: refused (once), nothing adopted */
        for (int i=0;i<400;++i,t+=16667) { int raw[4]={0,0,0,-29000}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cy[1]==0);
        /* detection off (BB_STICK_AUTOCENTER=0) */
        c.auto_state=2;
        for (int i=0;i<400;++i,t+=16667) { int raw[4]={0,0,0,-21000}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cy[1]==0);
        /* the left stick is detected too, independently */
        cal_connect(&c,t); c.auto_state=1;
        for (int i=0;i<300;++i,t+=16667) { int raw[4]={12000,3000,0,0}; cal_stuck(&c,raw,dz2,t); }
        assert(c.cx[0]==12000 && c.cy[0]==3000 && c.cx[1]==0 && c.cy[1]==0);
        c.cx[0]=c.cy[0]=0;
        /* re-centre: -23630 then F9 at -20840 */
        c.cx[1]=0; c.cy[1]=-23630;
        { int raw[4]={0,0,0,-20840}; cal_recentre(&c,raw,dz2,""); }
        assert(c.cx[1]==0 && c.cy[1]==-20840 && c.cx[0]==0 && c.cy[0]==0);    /* the left stick at rest keeps (0,0) */
        { int raw[4]={0,0,0,0}; cal_recentre(&c,raw,dz2,""); }
        assert(c.cy[1]==0);                                                   /* at rest again: back to (0,0) */
        /* auto-release: 3 s of raw (0,0) while the output would be a big deflection */
        c.cx[1]=0; c.cy[1]=-23630; c.rest_since[1]=0;
        { int raw[4]={0,0,0,0}; uint64_t u=t;
          cal_release(&c,raw,dz2,u); assert(c.cy[1]==-23630);
          cal_release(&c,raw,dz2,u+2900000); assert(c.cy[1]==-23630);
          cal_release(&c,raw,dz2,u+3000000); assert(c.cx[1]==0 && c.cy[1]==0); }
        /* ... not while the stick still rests at the faulty point, and a move away restarts the timer */
        c.cy[1]=-23630;
        { int rest[4]={0,0,0,-23630}, zero[4]={0,0,0,0}; uint64_t u=t;
          cal_release(&c,rest,dz2,u); cal_release(&c,rest,dz2,u+5000000); assert(c.cy[1]==-23630);
          cal_release(&c,zero,dz2,u+6000000); cal_release(&c,rest,dz2,u+8000000);
          cal_release(&c,zero,dz2,u+8500000); cal_release(&c,zero,dz2,u+11000000); assert(c.cy[1]==-23630);
          cal_release(&c,zero,dz2,u+11500000); assert(c.cy[1]==0); }
        /* refusal above 85 % (and just below it accepted) */
        c.cx[1]=c.cy[1]=0;
        { int raw[4]={0,0,0,-29000}; cal_recentre(&c,raw,dz2,""); assert(c.cx[1]==0 && c.cy[1]==0); }
        { int raw[4]={0,0,0,-27800}; cal_recentre(&c,raw,dz2,""); assert(c.cy[1]==-27800); }
        { int raw[4]={20000,0,0,20000}; cal_recentre(&c,raw,dz2,""); assert(c.cx[0]==20000 && c.cy[1]==20000 && c.cx[1]==0); }
        c.cx[0]=c.cy[1]=0;
        /* ---- rest-point memory (round 8) ---- */
        {
            PadCal m; uint64_t t2=5000000, hit;
            const int zero=0;
            cal_connect(&m,t2); m.auto_state=1; assert(m.mem==1);
            strcpy(m.key,"test:0001:guidA");
            /* B learned once, by the 3 s stuck detection (and saved) */
            t2=feed(&m,t2,0,-1,1000,0,NULL,NULL);
            t2=feed(&m,t2,1640,-19502,3300,20,NULL,NULL);
            assert(m.rp_n[1]==1 && SDL_abs(m.rp_x[1][0]-1640)<=20 && SDL_abs(m.rp_y[1][0]+19502)<=20);
            assert(m.rp_n[0]==0 && SDL_abs(m.cy[1]+19502)<=20);
            /* A after B: settle near 0 -> centre (0,0) within 250 ms (+ one sample), not before */
            t2=sweep(&m,t2,1640,-19502,0,-1,300);
            assert(m.cy[1]!=0);                                                    /* still moving: no snap */
            const uint64_t tA=t2;
            t2=feed(&m,t2,0,-1,600,20,&hit,&zero);
            assert(m.cx[1]==0 && m.cy[1]==0 && hit);
            assert(hit-tA>=REST_STILL_US && hit-tA<=REST_STILL_US+SAMPLE_US);
            /* later: move -> settle at B +-1500 -> centre = that value within 250 ms */
            t2=sweep(&m,t2,0,-1,2800,-20900,300);
            t2=feed(&m,t2,2800,-20900,600,20,&hit,NULL);
            assert(SDL_abs(m.cx[1]-2800)<=20 && SDL_abs(m.cy[1]+20900)<=20);       /* the exact current value */
            assert(m.rp_n[1]==1);                                                  /* snapping learns nothing new */
            /* ... and the snap comes at 250 ms of stillness (known point (1640,-19502), settle 1800 away) */
            { PadCal q; cal_connect(&q,0); q.auto_state=1; q.rp_n[1]=1; q.rp_x[1][0]=1640; q.rp_y[1][0]=-19502;
              uint64_t u=1000000, first=0, u0=u;
              for (int i=0;i<40;++i,u+=SAMPLE_US) {
                  const int raw[4]={0,-1,2800+(i%3-1)*20,-20900+((i/3)%3-1)*20};
                  cal_update(&q,raw,(const int[2]){7849,8689},u);
                  if (!first && q.cy[1]) first=u;
              }
              assert(first && first-u0>=REST_STILL_US && first-u0<=REST_STILL_US+SAMPLE_US);
              assert(SDL_abs(q.cx[1]-2800)<=20 && SDL_abs(q.cy[1]+20900)<=20); }
            /* a new, unknown rest point: 3 s detection, then remembered */
            t2=sweep(&m,t2,2800,-20900,-15000,9000,300);
            const uint64_t tN=t2;
            uint64_t t3=feed(&m,t2,-15000,9000,2850,10,NULL,NULL);
            assert(m.cy[1]!=9000 && m.rp_n[1]==1);                                 /* not at 2.85 s */
            t3=feed(&m,t3,-15000,9000,400,10,&hit,NULL);
            assert(SDL_abs(m.cy[1]-9000)<=10 && m.rp_n[1]==2 && t3-tN>=3000000);
            t2=sweep(&m,t3,-15000,9000,0,-1,300);
            t2=feed(&m,t2,0,-1,400,10,NULL,NULL); assert(m.cx[1]==0 && m.cy[1]==0);
            t2=sweep(&m,t2,0,-1,-15600,8400,300);
            t2=feed(&m,t2,-15600,8400,600,10,&hit,NULL);
            assert(SDL_abs(m.cx[1]+15600)<=10 && SDL_abs(m.cy[1]-8400)<=10 && m.rp_n[1]==2);   /* merged: one point */
            /* persistence: save -> load -> the first settle at B snaps in 250 ms, no 3 s wait */
            {
                FILE *rf=fopen(restfile,"r"); assert(rf);
                char buf[800]; size_t nb=fread(buf,1,sizeof buf-1,rf); buf[nb]=0; fclose(rf);
                assert(strstr(buf,"test:0001:guidA 1 ") && !strstr(buf,"guidB"));
                PadCal l; cal_connect(&l,0); l.auto_state=1; strcpy(l.key,"test:0001:guidA"); rest_load(&l);
                assert(l.rp_n[1]==2 && l.rp_n[0]==0);
                uint64_t u=1000000, u0, first=0;
                u=feed(&l,u,0,-1,500,10,NULL,NULL);
                u=sweep(&l,u,0,-1,1000,-20200,300);
                u0=u;
                for (int i=0;i<40;++i,u+=SAMPLE_US) {
                    const int raw[4]={0,-1,1000+(i%3-1)*20,-20200+((i/3)%3-1)*20};
                    cal_update(&l,raw,(const int[2]){7849,8689},u);
                    if (!first && l.cy[1]) first=u;
                }
                assert(first && first-u0<=REST_STILL_US+SAMPLE_US && SDL_abs(l.cy[1]+20200)<=20);
                /* another controller does not see these points; the file keeps both controllers */
                PadCal o; cal_connect(&o,0); o.auto_state=1; strcpy(o.key,"test:0002:guidB"); rest_load(&o);
                assert(o.rp_n[1]==0);
                uint64_t w=feed(&o,1000000,0,-1,500,10,NULL,NULL);
                w=feed(&o,w,500,-19000,3300,10,NULL,NULL); assert(o.rp_n[1]==1);
                PadCal l2; cal_connect(&l2,0); l2.auto_state=1; strcpy(l2.key,"test:0001:guidA"); rest_load(&l2);
                assert(l2.rp_n[1]==2);
                PadCal o2; cal_connect(&o2,0); o2.auto_state=1; strcpy(o2.key,"test:0002:guidB"); rest_load(&o2);
                assert(o2.rp_n[1]==1);
                /* a manually edited point beyond 85 % is never snapped to */
                PadCal h; cal_connect(&h,0); h.auto_state=1; h.rp_n[1]=1; h.rp_x[1][0]=0; h.rp_y[1][0]=-28500;
                uint64_t z=feed(&h,1000000,0,-28500,1000,10,NULL,NULL); (void)z;
                assert(h.cy[1]==0);
            }
            /* a human: a slow sweep through B (1500 units/s) and a hold with +-300 jitter never snap */
            { PadCal h; cal_connect(&h,0); h.auto_state=1; h.rp_n[1]=1; h.rp_x[1][0]=1640; h.rp_y[1][0]=-19502;
              uint64_t u=1000000;
              u=sweep(&h,u,0,-24000,0,-14000,6600);
              assert(h.cy[1]==0);
              u=feed(&h,u,0,-19500,2500,300,NULL,NULL); assert(h.cy[1]==0);
              (void)u; }
            /* more than four points: the oldest is dropped; points within 3000 merge */
            { PadCal h; cal_connect(&h,0); h.auto_state=1;
              rest_learn(&h,1,10000,0); rest_learn(&h,1,-10000,0); rest_learn(&h,1,0,10000); rest_learn(&h,1,0,-10000);
              assert(h.rp_n[1]==4);
              rest_learn(&h,1,12000,-12000);
              assert(h.rp_n[1]==4);
              { int has_first=0; for (int i=0;i<4;++i) if (h.rp_x[1][i]==10000 && h.rp_y[1][i]==0) has_first=1; assert(!has_first); }
              rest_learn(&h,1,-9000,1500);                 /* 1800 from (-10000,0): the same point, new value */
              assert(h.rp_n[1]==4);
              { int found=0; for (int i=0;i<4;++i) if (h.rp_x[1][i]==-9000 && h.rp_y[1][i]==1500) found=1; assert(found); } }
            /* BB_STICK_REST_MEMORY=0: round-5 behaviour (stuck capture, 3 s auto-release), nothing learned or saved */
            { unlink(restfile);
              set_env("BB_STICK_REST_MEMORY","0");
              PadCal h; cal_connect(&h,0); h.auto_state=1; strcpy(h.key,"test:0003:guidC"); assert(h.mem==0);
              uint64_t u=feed(&h,1000000,0,-1,1000,10,NULL,NULL);
              u=feed(&h,u,1000,-20500,3300,20,NULL,NULL);
              assert(SDL_abs(h.cy[1]+20500)<=20 && h.rp_n[1]==0 && access(restfile,F_OK)!=0);
              u=sweep(&h,u,1000,-20500,0,-1,300);
              u=feed(&h,u,0,-1,1000,10,NULL,NULL); assert(h.cy[1]!=0);          /* no fast snap */
              u=feed(&h,u,0,-1,2300,10,NULL,NULL); assert(h.cy[1]==0);          /* released after 3 s of rest */
              unsetenv("BB_STICK_REST_MEMORY");
              PadCal mm; cal_connect(&mm,0); assert(mm.mem==1);
              /* detection off (BB_STICK_AUTOCENTER=0) also means no memory */
              mm.auto_state=2; assert(!cal_memory_on(&mm)); }

            /* ---- dead corridor (round 9) ---- */
            {
                const int dzr=8689, PX=1143, PY=-20709;
                PadCal q; cal_connect(&q,0); q.auto_state=1; q.corr=1; q.rp_n[1]=1; q.rp_x[1][0]=PX; q.rp_y[1][0]=PY;
                assert(cor_enabled(&q) && cor_any(&q,1) && !cor_any(&q,0));
                assert(cor_halfwidth(dzr)==dzr && cor_halfwidth(2000)==1.5*COR_SLIDE_DEV);   /* max(deadzone, 1.5 x slide) */
                /* a slide 0 -> P sampled every 16 ms stays (128,128) all the way, from either snapped centre, also
                 * 2906 off the line (the largest in the logs) to both sides, and with the whole logic running */
                for (int state=0; state<2; ++state) for (int dev=-2906; dev<=2906; dev+=2906) {
                    PadCal w=q; w.cx[1]=state ? PX : 0; w.cy[1]=state ? PY : 0;
                    uint64_t u=1000000;
                    for (int i=0;i<=200;++i,u+=SAMPLE_US) {
                        const int rx=PX*i/200+dev*(i>=10 && i<200), ry=PY*i/200, raw[4]={0,-1,rx,ry};
                        cal_update(&w,raw,(const int[2]){7849,dzr},u);
                        int16_t ox, oy; stick_output(&w,1,(int16_t)rx,(int16_t)ry,dzr,&ox,&oy);
                        assert(axis(ox)==128 && axis(oy)==128);
                    }
                }
                /* and 1500 past P (the extension) and at P with x +- the half-width */
                { int16_t ox, oy;
                  stick_output(&q,1,(int16_t)PX,(int16_t)(PY-1400),dzr,&ox,&oy); assert(axis(ox)==128 && axis(oy)==128);
                  stick_output(&q,1,(int16_t)(PX+8000),(int16_t)PY,dzr,&ox,&oy); assert(axis(ox)==128 && axis(oy)==128);
                  stick_output(&q,1,(int16_t)(PX-8000),(int16_t)PY,dzr,&ox,&oy); assert(axis(ox)==128 && axis(oy)==128); }
                /* full up beyond P reaches 0; down, left, right are what round 8 sent (centre (0,0)) */
                { int16_t ox, oy, rx, ry;
                  stick_output(&q,1,0,-32768,dzr,&ox,&oy); assert(axis(oy)==0);
                  stick_output(&q,1,PX,-32768,dzr,&ox,&oy); assert(axis(oy)==0);
                  const int pts[4][2]={{0,32767},{-32768,0},{32767,0},{0,-1}};
                  for (int i=0;i<4;++i) {
                      stick_output(&q,1,(int16_t)pts[i][0],(int16_t)pts[i][1],dzr,&ox,&oy);
                      round8_output(&q,1,pts[i][0],pts[i][1],dzr,&rx,&ry);
                      assert(axis(ox)==axis(rx) && axis(oy)==axis(ry));
                  }
                  stick_output(&q,1,0,32767,dzr,&ox,&oy); assert(axis(oy)==255);
                  stick_output(&q,1,-32768,0,dzr,&ox,&oy); assert(axis(ox)==0);
                  stick_output(&q,1,32767,0,dzr,&ox,&oy); assert(axis(ox)==255); }
                /* monotonic and no jump crossing the corridor edge: up along the corridor (both snapped centres),
                 * sideways at mid height, and across the near end */
                for (int state=0; state<2; ++state) {
                    PadCal w=q; w.cx[1]=state ? PX : 0; w.cy[1]=state ? PY : 0;
                    int16_t px_=0, py_=0, ox, oy; int first=1, maxjump=0, left_zero=0;
                    for (int y=-19000; y>=-32768; --y) {
                        stick_output(&w,1,(int16_t)PX,(int16_t)y,dzr,&ox,&oy);
                        if (!first) { assert(oy<=py_); const int j=SDL_abs(oy-py_); if (j>maxjump) maxjump=j; }
                        if (oy==0 && ox==0) left_zero=y;
                        px_=ox; py_=oy; first=0;
                    }
                    assert(maxjump<=8);                                   /* ~3 per raw unit at most */
                    /* centre (0,0): dead up to the corridor edge (|P|+1500 = 22240 along the axis, y ~ -22210);
                     * centre P: the round-8 scaled deadzone around P is the larger one (about P + 3200) */
                    if (state==0) assert(left_zero<=-22150 && left_zero>=-22300);
                    else assert(left_zero<=-23700 && left_zero>=-24100);
                    assert(py_<-32000);
                    first=1; maxjump=0;
                    for (int x=0; x<=32767; ++x) {
                        stick_output(&w,1,(int16_t)x,-12000,dzr,&ox,&oy);
                        if (!first) { assert(ox>=px_); const int j=SDL_abs(ox-px_)+SDL_abs(oy-py_); if (j>maxjump) maxjump=j; }
                        px_=ox; py_=oy; first=0;
                    }
                    assert(maxjump<=12);
                    first=1; maxjump=0;                                    /* down through the near end, x = 0 */
                    for (int y=-3000; y<=32767; ++y) {
                        stick_output(&w,1,0,(int16_t)y,dzr,&ox,&oy);
                        if (!first) { assert(oy>=py_); const int j=SDL_abs(oy-py_)+SDL_abs(ox-px_); if (j>maxjump) maxjump=j; }
                        px_=ox; py_=oy; first=0;
                    }
                    assert(maxjump<=12);
                }
                /* a stick without a learned point, a point too near the centre and one beyond 85 %: byte-identical
                 * to round 8, for the true centre and a snapped one */
                for (int variant=0; variant<4; ++variant) {
                    PadCal w; cal_connect(&w,0); w.auto_state=1; w.corr=1;
                    if (variant==1) { w.rp_n[1]=1; w.rp_x[1][0]=0; w.rp_y[1][0]=-9000; }        /* 27 %: deadzone covers it */
                    if (variant==2) { w.rp_n[1]=1; w.rp_x[1][0]=0; w.rp_y[1][0]=-29000; }       /* 88 %: refused */
                    if (variant==3) { w.rp_n[0]=1; w.rp_x[0][0]=0; w.rp_y[0][0]=-20000; }       /* left point: right unaffected */
                    for (int state=0; state<2; ++state) {
                        w.cx[1]=state ? PX : 0; w.cy[1]=state ? PY : 0;
                        for (int x=-32768; x<=32767; x+=1111) for (int y=-32768; y<=32767; y+=1109) {
                            int16_t ox, oy, rx, ry;
                            stick_output(&w,1,(int16_t)x,(int16_t)y,dzr,&ox,&oy);
                            round8_output(&w,1,x,y,dzr,&rx,&ry);
                            assert(ox==rx && oy==ry);
                        }
                    }
                }
                /* BB_STICK_CORRIDOR=0: byte-identical to round 8, with the learned point present */
                set_env("BB_STICK_CORRIDOR","0");
                { PadCal w; cal_connect(&w,0); w.auto_state=1; assert(w.corr==0);
                  w.rp_n[1]=1; w.rp_x[1][0]=PX; w.rp_y[1][0]=PY;
                  for (int state=0; state<2; ++state) {
                      w.cx[1]=state ? PX : 0; w.cy[1]=state ? PY : 0;
                      for (int x=-32768; x<=32767; x+=1111) for (int y=-32768; y<=32767; y+=1109) {
                          int16_t ox, oy, rx, ry;
                          stick_output(&w,1,(int16_t)x,(int16_t)y,dzr,&ox,&oy);
                          round8_output(&w,1,x,y,dzr,&rx,&ry);
                          assert(ox==rx && oy==ry);
                      }
                  }
                  /* ... and the snap guard is off with it: a still hold beyond the edge snaps as in round 8 */
                  uint64_t u=feed(&w,1000000,PX,-23000,600,10,NULL,NULL); (void)u;
                  assert(w.cy[1]!=0); }
                unsetenv("BB_STICK_CORRIDOR");
#ifdef __APPLE__
                { PadCal w; cal_connect(&w,0); assert(w.corr==1); }          /* default on where the rest memory is on */
#else
                { PadCal w; cal_connect(&w,0); assert(w.corr==0); }
#endif
                /* snapping: held still inside the corridor snaps as before, held still beyond its edge (a deliberate
                 * look, 2300 past P but within the snap radius) does not */
                { PadCal w=q; uint64_t u=feed(&w,1000000,PX+50,PY+300,600,10,NULL,NULL);
                  assert(SDL_abs(w.cy[1]-(PY+300))<=10);
                  PadCal v=q; u=feed(&v,1000000,PX,-23000,600,10,NULL,NULL); (void)u;
                  assert(v.cx[1]==0 && v.cy[1]==0);
                  u=feed(&v,1000000,0,-1,600,10,NULL,NULL); assert(v.cx[1]==0 && v.cy[1]==0); }
                puts("PASS: stick dead corridor (slide stays centre, full deflection, monotonic, no jump, byte-identical without a point or with BB_STICK_CORRIDOR=0)");
            }
        }
    }
    /* through the SDL gamepad: untouched stick with a rest offset sends exactly 128 */
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,3000);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-2500);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-2000);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.right_x==128 && data.right_y==128 && data.left_x==128);
    /* a learned centre through the SDL gamepad: the faulty rest sends 128, a full push still reaches 0/255 */
    cal.auto_state=2; cal.cx[1]=0; cal.cy[1]=-23630;
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,0);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-23630);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.right_x==128 && data.right_y==128);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,32767);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.right_y==255);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-32768);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.right_y==0);
    /* F9 (a window key event) re-centres; so does LB + RB + right-stick click held 1 s, and View + right-stick click */
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-20840);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-23630);
    f9_request=1;
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-20840 && f9_request==0);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-18000);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-20840);                   /* no R3 yet: no chord */
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_STICK,true);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-20840);                   /* held for less than 1 s */
    usleep(1100000);
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-18000);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-16000);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-18000);                   /* chord released: re-arms */
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,true);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-18000);
    usleep(1100000);
    assert(pad_read_state(1,&data)==0 && cal.cy[1]==-16000);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,false);
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_STICK,false);
    cal.cy[1]=0;
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,0);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,0);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    unlink(restfile);
    puts("PASS: pad ABI, debug camera chord, left/right clicks, SDL touch, vibration controls, overlay capture");
}
