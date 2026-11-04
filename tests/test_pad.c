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

int main(void) {
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
    puts("PASS: pad ABI, debug camera chord, left/right clicks, SDL touch, vibration controls, overlay capture");
}
