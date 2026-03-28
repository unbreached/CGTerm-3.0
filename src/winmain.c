/*
 * Minimal WinMain entry point for SDL 1.2 on Windows.
 * Replaces SDLmain.lib which has compatibility issues with modern MinGW.
 */
#ifdef WINDOWS
#include <windows.h>
#include <SDL.h>

extern int main(int argc, char *argv[]);

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInst;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    SDL_SetModuleHandle(GetModuleHandle(NULL));
    return main(__argc, __argv);
}
#endif
