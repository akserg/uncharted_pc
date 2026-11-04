#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
_Thread_local int runtime_ndgi_open; /* normally defined in runtime_fiber.c; pad test does not link it */
#include "../src/runtime_pad.c"

static int capture;
static int rumble_calls;
static uint16_t rumble_low, rumble_high;
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
    /* through the SDL gamepad: untouched stick with a rest offset sends exactly 128 */
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,3000);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-2500);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-2000);
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.right_x==128 && data.right_y==128 && data.left_x==128);
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
