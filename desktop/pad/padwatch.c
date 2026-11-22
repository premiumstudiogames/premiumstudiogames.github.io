/* PremiumPad: the controllers' Home button for Premium Games (console mode).
   SDL reads every controller in the background (also while another program - a game - has the focus):
   Xbox pads (the Xbox button, through XInput) and PlayStation pads (the PS button, through HID).
   Out (stdout, one line each):  "ready", "pad <ps|xbox|other> <name>", "unpad", "home 1 <pid>" (pressed; pid = the program in front, 0 if none), "home 0" (let go).
   View + Menu (Back + Start) together also count as Home.
   In (stdin): "test" -> a short Home press (for the tests); "focus <pid>" -> that program's window to the front
   (a game in its own program, back from the console); the program ends when stdin closes (the app quit).
   "PremiumPad --focus <pid>" only brings that window to the front and ends. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

static volatile int quit = 0, test = 0;
static void out(const char* s) { fputs(s, stdout); fputc('\n', stdout); fflush(stdout); }
#ifdef _WIN32
/* the game's main window: visible, top level, not owned by another window */
typedef struct { DWORD pid; HWND w; } Find;
static BOOL CALLBACK findWin(HWND w, LPARAM l) { Find* f = (Find*)l; DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
  if (pid == f->pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) { f->w = w; return FALSE; } return TRUE; }
static int focusPid(DWORD pid) {
  Find f = { pid, NULL }; EnumWindows(findWin, (LPARAM)&f); if (!f.w) return 0;
  if (IsIconic(f.w)) ShowWindow(f.w, SW_RESTORE);
  /* Windows only lets the program the player is using change the front window: the input of the current front
     window is joined for a moment (and an Alt tap counts as the player's own input), then the game comes forward */
  /* the other program takes the front a moment later (its own thread activates it): wait for that, and ask again if a
     window that was closing (the Control Center) or nothing was in front at the first try */
  for (int tries = 0; tries < 4 && GetForegroundWindow() != f.w; tries++) {
    HWND fg = GetForegroundWindow(); DWORD fgT = fg ? GetWindowThreadProcessId(fg, NULL) : 0, me = GetCurrentThreadId();
    if (fgT && fgT != me) AttachThreadInput(me, fgT, TRUE);
    keybd_event(VK_MENU, 0, 0, 0); keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    BringWindowToTop(f.w); SetForegroundWindow(f.w); SetFocus(f.w);
    if (fgT && fgT != me) AttachThreadInput(me, fgT, FALSE);
    for (int i = 0; i < 10 && GetForegroundWindow() != f.w; i++) Sleep(15);
  }
  return GetForegroundWindow() == f.w;
}
/* the program in front when Home is pressed: Premium Games then knows whether it was over a game, an app or something else */
static void homeDown(void) { char line[48]; DWORD pid = 0; HWND fg = GetForegroundWindow(); if (fg) GetWindowThreadProcessId(fg, &pid); snprintf(line, sizeof line, "home 1 %lu", (unsigned long)pid); out(line); }
static DWORD WINAPI readIn(LPVOID p) { (void)p; char b[64];
  while (fgets(b, sizeof b, stdin)) {
    if (!strncmp(b, "test", 4)) test = 1;
    else if (!strncmp(b, "focus ", 6)) { char line[64]; DWORD pid = (DWORD)strtoul(b + 6, NULL, 10); snprintf(line, sizeof line, "focused %lu %d", (unsigned long)pid, pid ? focusPid(pid) : 0); out(line); }
  }
  quit = 1; return 0; }
#else
static void homeDown(void) { out("home 1 0"); }
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  if (argc == 3 && !strcmp(argv[1], "--focus")) return focusPid((DWORD)strtoul(argv[2], NULL, 10)) ? 0 : 1;
#else
  (void)argc; (void)argv;
#endif
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
    if (test) { test = 0; homeDown(); SDL_Delay(80); out("home 0"); }
    if (now != home) { home = now; if (home) homeDown(); else out("home 0"); }
  }
  SDL_Quit();
  return 0;
}
