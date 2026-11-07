/* 13_sentinels_launcher.exe: opens the launcher of 13_sentinels_aegis_rim.exe, which owns the launcher and the game. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show) {
    (void)instance; (void)previous; (void)arguments; (void)show;
    wchar_t path[32768], command[32768];
    const DWORD length = GetModuleFileNameW(NULL, path, 32768);
    if (!length || length >= 32768) return 1;
    wchar_t *name = wcsrchr(path, L'\\');
    if (!name) return 1;
    name[1] = 0;
    wcscat_s(path, 32768, L"13_sentinels_aegis_rim.exe");
    if (swprintf(command, 32768, L"\"%ls\" --launcher", path) < 0) return 1;
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        MessageBoxW(NULL, L"13_sentinels_aegis_rim.exe was not found next to this program.",
                    L"13 Sentinels: Aegis Rim PC Port", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
