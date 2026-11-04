/* pad_probe: standalone SDL gamepad diagnostic (M5 triage round 3).
 * usage: pad_probe [SECONDS] [HINT=VALUE ...]   hints are set before SDL_Init (e.g. SDL_JOYSTICK_HIDAPI=0).
 * Leave the sticks and triggers untouched; prints once a second. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int seconds = 10, i = 1;
    if (i < argc && !strchr(argv[i], '=')) seconds = atoi(argv[i++]);
    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) continue;
        *eq = 0;
        SDL_SetHint(argv[i], eq + 1);
        printf("hint %s=%s\n", argv[i], eq + 1);
    }
    SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_VERBOSE);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    SDL_Gamepad *pad = NULL;
    for (int t = 0; t < 30 && !pad; t++) { /* wait up to 3 s for enumeration */
        SDL_PumpEvents();
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        if (ids && n > 0) pad = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
        if (!pad) SDL_Delay(100);
    }
    if (!pad) {
        int n = 0;
        SDL_JoystickID *js = SDL_GetJoysticks(&n);
        printf("no gamepad; %d raw joysticks\n", n);
        for (int j = 0; j < n; j++)
            printf("  joystick %u: %s vid=%04x pid=%04x\n", js[j], SDL_GetJoystickNameForID(js[j]),
                   SDL_GetJoystickVendorForID(js[j]), SDL_GetJoystickProductForID(js[j]));
        SDL_free(js);
        return 2;
    }
    SDL_Joystick *joy = SDL_GetGamepadJoystick(pad);
    char guid[64];
    SDL_GUIDToString(SDL_GetJoystickGUID(joy), guid, sizeof guid);
    char *mapping = SDL_GetGamepadMapping(pad);
    printf("name=%s\npath=%s\nguid=%s\nvid=%04x pid=%04x type=%d joytype=%d\nmapping=%s\n",
           SDL_GetGamepadName(pad), SDL_GetGamepadPath(pad) ? SDL_GetGamepadPath(pad) : "(null)", guid,
           SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad), (int)SDL_GetGamepadType(pad),
           (int)SDL_GetJoystickType(joy), mapping ? mapping : "(null)");
    SDL_free(mapping);
    const int axes = SDL_GetNumJoystickAxes(joy);
    printf("joystick axes=%d buttons=%d hats=%d\n", axes, SDL_GetNumJoystickButtons(joy), SDL_GetNumJoystickHats(joy));
    for (int s = 0; s < seconds; s++) {
        for (int t = 0; t < 10; t++) { SDL_PumpEvents(); SDL_Delay(100); }
        printf("t=%2d raw:", s + 1);
        for (int a = 0; a < axes; a++) printf(" a%d=%d", a, SDL_GetJoystickAxis(joy, a));
        printf(" | gamepad LX=%d LY=%d RX=%d RY=%d LT=%d RT=%d\n",
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY),
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY),
               SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }
    SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}
