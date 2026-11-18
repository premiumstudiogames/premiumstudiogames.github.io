/* PremiumPad: the controllers' Home button for Premium Games (console mode).
   SDL reads every controller in the background (also while another program - a game - has the focus):
   Xbox pads (the Xbox button, through XInput) and PlayStation pads (the PS button, through HID).
   Out (stdout, one line each):  "ready", "pad <ps|xbox|other> <name>", "unpad", "home 1" (pressed), "home 0" (let go).
   View + Menu (Back + Start) together also count as Home.
   In (stdin): "test" -> a short Home press (for the tests); the program ends when stdin closes (the app quit). */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static volatile int quit = 0, test = 0;
#ifdef _WIN32
static DWORD WINAPI readIn(LPVOID p) { (void)p; char b[64]; while (fgets(b, sizeof b, stdin)) { if (!strncmp(b, "test", 4)) test = 1; } quit = 1; return 0; }
#endif
static void out(const char* s) { fputs(s, stdout); fputc('\n', stdout); fflush(stdout); }

int main(void) {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "0");        /* don't change how the pad talks to games */
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH_HOME_LED, "0");
  if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { out("error"); return 1; }
#ifdef _WIN32
  CreateThread(NULL, 0, readIn, NULL, 0, NULL);
#endif
  out("ready");
  int home = 0;
  while (!quit) {
    SDL_Event e;
    while (SDL_WaitEventTimeout(&e, 30)) {
      if (e.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
        if (c) { SDL_GameControllerType t = SDL_GameControllerGetType(c); char line[200];
          const char* k = (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4 || t == SDL_CONTROLLER_TYPE_PS5) ? "ps" : (t == SDL_CONTROLLER_TYPE_XBOX360 || t == SDL_CONTROLLER_TYPE_XBOXONE) ? "xbox" : "other";
          snprintf(line, sizeof line, "pad %s %s", k, SDL_GameControllerName(c) ? SDL_GameControllerName(c) : ""); out(line); }
      } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) out("unpad");
    }
    /* Home on any pad: the guide button, or Back + Start together */
    int now = 0, cb = 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++) { SDL_GameController* c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i)); if (!c) continue;
      if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_GUIDE)) now = 1;
      if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK) && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)) cb = 1; }
    now = now || cb;
    if (test) { test = 0; out("home 1"); SDL_Delay(80); out("home 0"); }
    if (now != home) { home = now; out(home ? "home 1" : "home 0"); }
  }
  SDL_Quit();
  return 0;
}
