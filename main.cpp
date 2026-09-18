#include <windows.h>
#include <cstdio>
#include <string>

static HHOOK g_hook = nullptr;
static FILE* g_log = nullptr;

static bool g_ctrlPhysicallyDown = false;
static bool g_winPhysicallyDown = false;
static bool g_composeActive = false;
static bool g_swallowNextAltUp = false;
static bool g_swallowNextWinUp = false;

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
    g_swallowNextWinUp = false;
}

LRESULT CALLBACK
LowLevelKeyboardProc(const int nCode, const WPARAM wParam, const LPARAM lParam)
{
    if (nCode != HC_ACTION) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
    const bool injected = (kb->flags & LLKHF_INJECTED) != 0;

    if (g_log)
    {
        fprintf(g_log, "vk=%lu down=%d up=%d inj=%d\n", kb->vkCode, isDown, isUp, injected);
        fflush(g_log);
    }

    if (injected) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    if (kb->vkCode == VK_LCONTROL || kb->vkCode == VK_RCONTROL || kb->vkCode == VK_CONTROL)
    {
        if (isDown)
        {
            g_ctrlPhysicallyDown = true;
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        if (isUp)
        {
            g_ctrlPhysicallyDown = false;
            if (g_swallowNextAltUp)
            {
                g_swallowNextAltUp = false;
                if (g_log) fprintf(g_log, "Swallowed Alt up\n");
                return 1;
            }
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) {
        if (isDown)
        {
            g_winPhysicallyDown = true;

            if (!g_composeActive && g_ctrlPhysicallyDown)
            {
                g_composeActive = true;
                g_swallowNextAltUp = true;
                g_swallowNextWinUp = true;

                INPUT ctrlUp = {};
                ctrlUp.type = INPUT_KEYBOARD;
                ctrlUp.ki.wVk = VK_CONTROL;
                ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &ctrlUp, sizeof(INPUT));

                if (g_log) fprintf(g_log, "Compose started (Alt+Win)\n");
                return 1;
            }

            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        if (isUp)
        {
            g_winPhysicallyDown = false;
            if (g_swallowNextWinUp)
            {
                g_swallowNextWinUp = false;
                if (g_log) fprintf(g_log, "Swallowed Win up\n");
                return 1;
            }
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    if (isDown && g_composeActive)
    {
        if (kb->vkCode == 'O') { EmitChar(L'\u00F6'); return 1; }
        if (kb->vkCode == 'S') { EmitChar(L'\u00DF'); return 1; }
        if (kb->vkCode == 'U') { EmitChar(L'\u00FC'); return 1; }
        if (kb->vkCode == 'A') { EmitChar(L'\u00E4'); return 1; }
        g_composeActive = false;
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static DWORD g_mainThreadId = 0;

static BOOL WINAPI
ConsoleCtrlHandler(const DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT)
    {
        PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

static bool DEBUG = false;
static FILE* file = nullptr;

int
main(const int argc, char* argv[])
{
    // in future
    for (int i = 1; i < argc; i++)
    {
        auto sargv = std::string(argv[i]);
        if (sargv == "--help" or sargv == "-h") return 0;
        if (sargv == "--version" or sargv == "-v") {}
        if (sargv == "--debug" or sargv == "-d") DEBUG = true;
        if (sargv == "--autoload") {}
        if (sargv == "--disable-autoload" or sargv == "-dal") {}
    }


    g_mainThreadId = GetCurrentThreadId();
    if (fopen_s(&file, "hook_log.txt", "w") != 0) return 1;
    g_log = file;

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hook)
    {
        fprintf(g_log, "SetWindowsHookEx failed, err=%lu\n", GetLastError());
        fclose(g_log);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    fclose(g_log);
    return 0;
}