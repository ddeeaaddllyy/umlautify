#include <windows.h>
#include <cstdio>
#include <string>

static HHOOK g_hook = nullptr;
static FILE* g_log = nullptr;

static bool g_altPhysicallyDown = false;
static bool g_composeActive = false;
static bool g_swallowNextAltUp = false;
static bool g_swallowNextEUp = false;

static void
SendUnicodeChar(const wchar_t ch)
{
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = 0;
    in[0].ki.wScan = ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;

    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    const UINT sent = SendInput(2, in, sizeof(INPUT));
    if (g_log) {
        fprintf(g_log, "SendInput sent %u, err %lu\n", sent, GetLastError());
        fflush(g_log);
    }
}

static void
EmitChar(const wchar_t ch)
{
    SendUnicodeChar(ch);
    g_composeActive = false;
    g_swallowNextEUp = false;
}

LRESULT CALLBACK
LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
    const bool injected = (kb->flags & LLKHF_INJECTED) != 0;

    if (g_log) {
        fprintf(g_log, "vk=%02X down=%d up=%d inj=%d\n", kb -> vkCode, isDown, isUp, injected);
        fflush(g_log);
    }

    if (injected) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    if (kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU || kb->vkCode == VK_MENU) {
        if (isDown) {
            g_altPhysicallyDown = true;
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        if (isUp) {
            g_altPhysicallyDown = false;
            if (g_swallowNextAltUp) {
                g_swallowNextAltUp = false;
                if (g_log) fprintf(g_log, "Swallowed Alt up\n");
                return 1;
            }
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    if (isDown) {
        if (!g_composeActive && kb->vkCode == 'E' && g_altPhysicallyDown) {
            g_composeActive = true;
            g_swallowNextAltUp = true;
            g_swallowNextEUp = true;

            INPUT altUp = {};
            altUp.type = INPUT_KEYBOARD;
            altUp.ki.wVk = VK_MENU;
            altUp.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &altUp, sizeof(INPUT));

            if (g_log) fprintf(g_log, "Compose started, injected Alt up\n");
            return 1;
        }

        if (g_composeActive) {
            if (kb->vkCode == 'O') { EmitChar(L'\u00F6'); return 1; }
            if (kb->vkCode == 'S') { EmitChar(L'\u00DF'); return 1; }
            if (kb->vkCode == 'U') { EmitChar(L'\u00FC'); return 1; }
            if (kb->vkCode == 'A') { EmitChar(L'\u00E4'); return 1; }
            g_composeActive = false;
        }
    }

    if (isUp) {
        if (g_swallowNextEUp && kb->vkCode == 'E') {
            g_swallowNextEUp = false;
            return 1;
        }
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static DWORD g_mainThreadId = 0;

static BOOL WINAPI
ConsoleCtrlHandler(const DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

static bool DEBUG = false;

int
main(const int argc, char* argv[])
{
    // in future
    for (int i = 1; i < argc; i++) {
        auto sargv = std::string(argv[i]);
        if (sargv == "--help" or sargv == "-h") {
            break;
        }
        if (sargv == "--version" or sargv == "-v") {}
        if (sargv == "--debug" or sargv == "-d") {
            DEBUG = true;
        }
        if (sargv == "--autoload") {}
        if (sargv == "--disable-autoload" or sargv == "-dal") {}
    }


    g_mainThreadId = GetCurrentThreadId();
    g_log = fopen("hook_log.txt", "w");
    if (!g_log) return 1;

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!g_hook) {
        fprintf(g_log, "SetWindowsHookEx failed, err=%lu\n", GetLastError());
        fclose(g_log);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    fclose(g_log);
    return 0;
}