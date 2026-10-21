/*
 * Switchy - switch keyboard layout with Caps Lock.
 * Windows layer: keyboard hook, tray icon, autostart. Built without the C runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include "logic.h"

#define TAG          ((ULONG_PTR)0x53575459)    /* 'SWTY': marks our own injected keys */
#define WM_APP_ACT   (WM_APP + 1)               /* wParam = SW_ACT_* bits from the hook */
#define WM_APP_ARM   (WM_APP + 2)               /* Caps Lock is held: start the stale timer */
#define WM_APP_TRAY  (WM_APP + 3)
#define TIMER_REHOOK 1
#define TIMER_STALE  2
#define TIMER_TRAY   3
#define TIMER_UNDO   4
#define UNDO_DELAY_MS 150
#define TIMER_CAPSOFF 5
#define TIMER_FULLSCREEN 6
#define FULLSCREEN_MS 1000                      /* games go full-screen after they get focus */
#define CAPSOFF_MS   80
#define REHOOK_MS    20000                      /* Windows drops slow hooks silently: re-install */
#define STALE_MS     500
#define TASK_NAME    L"Switchy"
#define RUN_KEY      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define CFG_KEY      L"Software\\Switchy"
#define TASK_TREE    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tree\\Switchy"

enum { ID_ENABLED = 1, ID_POPUP, ID_FULLSCREEN, ID_AUTOSTART, ID_AUTOSTART_ADMIN, ID_EXIT };

typedef struct {
    const wchar_t *enabled, *popup, *fullscreen, *autostart, *autostart_admin, *exit;
    const wchar_t *tip_on, *tip_off, *tip_admin, *tip_paused, *hook_error, *admin_later, *admin_failed;
} Strings;

static const Strings EN = {
    L"Enabled\tAlt+Caps Lock", L"Show layout pop-up", L"Leave Caps Lock to full-screen games", L"Start with Windows",
    L"Start with Windows as administrator (works in admin windows)", L"Exit",
    L"Switchy: on", L"Switchy: off", L" (administrator)", L" - paused: full-screen app",
    L"Could not install the keyboard hook.",
    L"Switchy will run as administrator from the next sign-in.",
    L"Could not change the administrator autostart task."
};
static const Strings RU = {
    L"\x0412\x043A\x043B\x044E\x0447\x0451\x043D\tAlt+Caps Lock",
    L"\x041F\x043E\x043A\x0430\x0437\x044B\x0432\x0430\x0442\x044C \x043E\x043A\x043D\x043E \x0440\x0430\x0441\x043A\x043B\x0430\x0434\x043E\x043A",
    L"\x041D\x0435 \x0442\x0440\x043E\x0433\x0430\x0442\x044C Caps Lock \x0432 \x043F\x043E\x043B\x043D\x043E\x044D\x043A\x0440\x0430\x043D\x043D\x044B\x0445 \x0438\x0433\x0440\x0430\x0445",
    L"\x0417\x0430\x043F\x0443\x0441\x043A\x0430\x0442\x044C \x0432\x043C\x0435\x0441\x0442\x0435 \x0441 Windows",
    L"\x0417\x0430\x043F\x0443\x0441\x043A\x0430\x0442\x044C \x043E\x0442 \x0430\x0434\x043C\x0438\x043D\x0438\x0441\x0442\x0440\x0430\x0442\x043E\x0440\x0430 (\x0440\x0430\x0431\x043E\x0442\x0430\x0435\x0442 \x0432 \x043E\x043A\x043D\x0430\x0445 \x0430\x0434\x043C\x0438\x043D\x0438\x0441\x0442\x0440\x0430\x0442\x043E\x0440\x0430)",
    L"\x0412\x044B\x0445\x043E\x0434",
    L"Switchy: \x0432\x043A\x043B\x044E\x0447\x0451\x043D", L"Switchy: \x0432\x044B\x043A\x043B\x044E\x0447\x0435\x043D",
    L" (\x0430\x0434\x043C\x0438\x043D\x0438\x0441\x0442\x0440\x0430\x0442\x043E\x0440)",
    L" - \x043F\x0430\x0443\x0437\x0430: \x043F\x043E\x043B\x043D\x043E\x044D\x043A\x0440\x0430\x043D\x043D\x043E\x0435 \x043F\x0440\x0438\x043B\x043E\x0436\x0435\x043D\x0438\x0435",
    L"\x041D\x0435 \x0443\x0434\x0430\x043B\x043E\x0441\x044C \x0443\x0441\x0442\x0430\x043D\x043E\x0432\x0438\x0442\x044C \x043F\x0435\x0440\x0435\x0445\x0432\x0430\x0442 \x043A\x043B\x0430\x0432\x0438\x0430\x0442\x0443\x0440\x044B.",
    L"Switchy \x0431\x0443\x0434\x0435\x0442 \x0437\x0430\x043F\x0443\x0441\x043A\x0430\x0442\x044C\x0441\x044F \x043E\x0442 \x0430\x0434\x043C\x0438\x043D\x0438\x0441\x0442\x0440\x0430\x0442\x043E\x0440\x0430 \x0441\x043E \x0441\x043B\x0435\x0434\x0443\x044E\x0449\x0435\x0433\x043E \x0432\x0445\x043E\x0434\x0430.",
    L"\x041D\x0435 \x0443\x0434\x0430\x043B\x043E\x0441\x044C \x0438\x0437\x043C\x0435\x043D\x0438\x0442\x044C \x0437\x0430\x0434\x0430\x0447\x0443 \x0430\x0432\x0442\x043E\x0437\x0430\x043F\x0443\x0441\x043A\x0430."
};

static struct {
    SwState st;
    HHOOK hook;
    HWND hwnd;
    HANDLE mutex;
    HICON icon_on, icon_off;
    UINT msg_taskbar;
    BOOL stale_armed, tray_added, elevated, skip_fullscreen;
    HWINEVENTHOOK fg_hook;
    DWORD started;
    const Strings *s;
    wchar_t self[MAX_PATH];
} g;

static wchar_t buf[4096]; /* scratch for paths, command lines, task XML */

/* ---------- keyboard ---------- */

static void send_keys(const WORD *vk, const BYTE *up, int n)
{
    INPUT in[8];
    for (int i = 0; i < n; i++) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = vk[i];
        in[i].ki.wScan = (WORD)MapVirtualKeyW(vk[i], MAPVK_VK_TO_VSC);
        in[i].ki.dwFlags = (up[i] ? KEYEVENTF_KEYUP : 0) | (vk[i] == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0);
        in[i].ki.time = 0;
        in[i].ki.dwExtraInfo = TAG;
    }
    SendInput((UINT)n, in, sizeof(INPUT)); /* one call: a real key can't land in the middle */
}

/* Press the language hotkey configured in Windows (Settings > Time & language >
 * Typing > Advanced keyboard settings > Input language hot keys). */
static void switch_layout(void)
{
    wchar_t v[4] = L"1";
    DWORD size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Keyboard Layout\\Toggle", L"Language Hotkey", RRF_RT_REG_SZ, NULL, v, &size);
    static const BYTE dduu[] = { 0, 0, 1, 1 };
    WORD k[4];
    switch (v[0]) {
    case L'2': k[0] = VK_LCONTROL; k[1] = VK_LSHIFT; k[2] = VK_LSHIFT; k[3] = VK_LCONTROL; break;
    case L'4': { static const WORD g1[] = { VK_OEM_3, VK_OEM_3 }; static const BYTE du[] = { 0, 1 };
                 send_keys(g1, du, 2); return; }
    case L'3': k[0] = VK_LWIN; k[1] = VK_SPACE; k[2] = VK_SPACE; k[3] = VK_LWIN; break; /* no hotkey set */
    default:   k[0] = VK_LMENU; k[1] = VK_LSHIFT; k[2] = VK_LSHIFT; k[3] = VK_LMENU; break;
    }
    send_keys(k, dduu, 4);
}

static void update_tray(void);

static void do_actions(unsigned a)
{
    if (a & SW_ACT_TOGGLE_ENABLED) update_tray();
    if (a & SW_ACT_POPUP_BEGIN) {
        static const WORD k[] = { VK_LWIN, VK_SPACE, VK_SPACE }; static const BYTE u[] = { 0, 0, 1 };
        send_keys(k, u, 3);
    }
    if (a & SW_ACT_TOGGLE_CAPS) {
        static const WORD k[] = { VK_CAPITAL, VK_CAPITAL }; static const BYTE u[] = { 0, 1 };
        BOOL was_on = GetKeyState(VK_CAPITAL) & 1;
        send_keys(k, u, 2);
        /* With "Turn off Caps Lock: press Shift" the Caps Lock key can only turn it on */
        if (was_on) SetTimer(g.hwnd, TIMER_CAPSOFF, CAPSOFF_MS, NULL);
    }
    if (a & SW_ACT_SWITCH) switch_layout();
    if (a & SW_ACT_POPUP_END) {
        static const WORD k[] = { VK_LWIN }; static const BYTE u[] = { 1 };
        send_keys(k, u, 1);
    }
    /* the flyout applies its choice asynchronously after Win is released: undo a bit later */
    if (a & SW_ACT_UNDO_SWITCH) SetTimer(g.hwnd, TIMER_UNDO, UNDO_DELAY_MS, NULL);
}

/* Caps Lock is still on after we tried to turn it off: Windows is set to turn it
 * off with Shift (erryox/Switchy#5). Tap the Shift the user is NOT holding, so
 * our key-up doesn't cancel the user's own Shift. */
static void caps_off_with_shift(void)
{
    if (!(GetKeyState(VK_CAPITAL) & 1)) return;
    WORD shift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? VK_RSHIFT : VK_LSHIFT;
    WORD k[2] = { shift, shift };
    static const BYTE u[] = { 0, 1 };
    send_keys(k, u, 2);
}

/* Back to the previous layout = forward through all the others. */
static void undo_switch(void)
{
    int n = GetKeyboardLayoutList(0, NULL);
    for (int i = 1; i < n; i++) switch_layout();
}

/* Runs for every key press in the session: keep it tiny. Real work is posted. */
static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
        /* Skip our own keys, and keys injected by a less privileged process: an
         * elevated Switchy must not turn them into keystrokes of its own (UIPI). */
        if (k->dwExtraInfo != TAG && !(k->flags & LLKHF_LOWER_IL_INJECTED)) {
            SwResult r = sw_on_key(&g.st, k->vkCode, wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN, k->time);
            if (r.actions) PostMessageW(g.hwnd, WM_APP_ACT, r.actions, 0);
            if (g.st.caps_held && !g.stale_armed) { g.stale_armed = TRUE; PostMessageW(g.hwnd, WM_APP_ARM, 0, 0); }
            if (r.swallow) return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* Install the new hook first, then drop the old one: no gap, no double delivery
 * (both calls run back to back on this thread, so no event is dispatched between). */
static BOOL install_hook(void)
{
    HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, hook_proc, GetModuleHandleW(NULL), 0);
    if (!h) return FALSE;
    if (g.hook) UnhookWindowsHookEx(g.hook);
    g.hook = h;
    return TRUE;
}

/* ---------- process helpers ---------- */

/* Wait for a handle while still pumping messages: the hook is served by this
 * thread, and blocking it would make Windows drop the hook. */
static DWORD wait_pumping(HANDLE h)
{
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &h, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1) break;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    DWORD code = 1;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return code;
}

static DWORD run_hidden(wchar_t *cmdline)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 1;
    CloseHandle(pi.hThread);
    return wait_pumping(pi.hProcess);
}

static DWORD run_self_elevated(const wchar_t *args)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g.hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = g.self;
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return 1;
    return wait_pumping(sei.hProcess);
}

static BOOL is_elevated(void)
{
    HANDLE t; TOKEN_ELEVATION e; DWORD n; BOOL r = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        if (GetTokenInformation(t, TokenElevation, &e, sizeof(e), &n)) r = e.TokenIsElevated != 0;
        CloseHandle(t);
    }
    return r;
}

/* Last exit or close attempt, for "it closed by itself" reports:
 * HKCU\Software\Switchy\LastExit = "<reason>, uptime <seconds> s, pid <pid>". */
static void log_exit(const wchar_t *reason)
{
    static wchar_t s[160];
    wsprintfW(s, L"%s, uptime %u s, pid %u", reason, (GetTickCount() - g.started) / 1000, GetCurrentProcessId());
    RegSetKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"LastExit", REG_SZ, s, (DWORD)(lstrlenW(s) + 1) * sizeof(wchar_t));
}

/* ---------- settings and autostart ---------- */

static DWORD cfg_get(const wchar_t *name, DWORD def)
{
    DWORD v, size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &size) == ERROR_SUCCESS ? v : def;
}

static void cfg_set(const wchar_t *name, DWORD v)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, CFG_KEY, name, REG_DWORD, &v, sizeof(v));
}

static BOOL startup_folder_path(const wchar_t *file)
{
    return ExpandEnvironmentStringsW(L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\", buf, MAX_PATH) &&
           lstrcatW(buf, file) != NULL;
}

/* Old instructions said "put Switchy.exe (or a shortcut) into shell:startup". */
static BOOL legacy_startup_exists(void)
{
    return (startup_folder_path(L"Switchy.exe") && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) ||
           (startup_folder_path(L"Switchy.lnk") && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES);
}

static void remove_legacy_startup(void)
{
    if (startup_folder_path(L"Switchy.lnk")) DeleteFileW(buf);
    if (startup_folder_path(L"Switchy.exe") && lstrcmpiW(buf, g.self) != 0) DeleteFileW(buf);
}

static BOOL run_autostart_exists(void)
{
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, L"Switchy", RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS ||
           legacy_startup_exists();
}

/* Both ways the old shell:startup copy goes: it would start the old version too. */
static void set_run_autostart(BOOL on)
{
    remove_legacy_startup();
    if (on) {
        wsprintfW(buf, L"\"%s\"", g.self);
        RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"Switchy", REG_SZ, buf, (DWORD)(lstrlenW(buf) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"Switchy");
    }
}

static BOOL task_exists(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TASK_TREE, 0, KEY_READ, &k) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(k);
    return TRUE;
}

/* Elevated helper: copy the exe to Program Files (an elevated task must not run a
 * file the user can overwrite) and register a logon task with highest privileges. */
static DWORD install_task(void)
{
    static wchar_t target[MAX_PATH], user[256], xml_path[MAX_PATH];
    DWORD n = 256;
    if (!ExpandEnvironmentStringsW(L"%ProgramFiles%\\Switchy", target, MAX_PATH)) return 2;
    CreateDirectoryW(target, NULL);
    lstrcatW(target, L"\\Switchy.exe");
    if (lstrcmpiW(target, g.self) != 0 && !CopyFileW(g.self, target, FALSE)) {
        wsprintfW(buf, L"%s.old", target);              /* a running copy can be renamed, not overwritten */
        DeleteFileW(buf);
        if (!MoveFileW(target, buf) || !CopyFileW(g.self, target, FALSE)) return 3;
        MoveFileExW(buf, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    if (!ExpandEnvironmentStringsW(L"%USERDOMAIN%\\", user, 256)) return 4;
    n = 256 - (DWORD)lstrlenW(user);
    if (!GetUserNameW(user + lstrlenW(user), &n)) return 4;

    lstrcpyW(buf, L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Description>Switchy: switch keyboard layout with Caps Lock</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><UserId>");
    lstrcatW(buf, user);
    lstrcatW(buf, L"</UserId></LogonTrigger></Triggers><Principals><Principal id=\"Author\"><UserId>");
    lstrcatW(buf, user);
    lstrcatW(buf, L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority></Settings>"
        L"<Actions Context=\"Author\"><Exec><Command>\"");
    lstrcatW(buf, target);
    lstrcatW(buf, L"\"</Command></Exec></Actions></Task>");

    if (!GetTempPathW(MAX_PATH, xml_path)) return 5;
    lstrcatW(xml_path, L"switchy-task.xml");
    HANDLE f = CreateFileW(xml_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return 5;
    DWORD w; WORD bom = 0xFEFF;
    BOOL ok = WriteFile(f, &bom, 2, &w, NULL) && WriteFile(f, buf, (DWORD)lstrlenW(buf) * 2, &w, NULL);
    CloseHandle(f);
    if (!ok) return 5;
    wsprintfW(buf, L"schtasks.exe /Create /TN \"" TASK_NAME L"\" /XML \"%s\" /F", xml_path);
    DWORD rc = run_hidden(buf);
    DeleteFileW(xml_path);
    return rc;
}

static DWORD uninstall_task(void)
{
    lstrcpyW(buf, L"schtasks.exe /Delete /TN \"" TASK_NAME L"\" /F");
    return run_hidden(buf);
}

static BOOL set_task_autostart(BOOL on)
{
    DWORD rc = g.elevated ? (on ? install_task() : uninstall_task())
                          : run_self_elevated(on ? L"--install-task" : L"--uninstall-task");
    if (rc != 0) MessageBoxW(g.hwnd, g.s->admin_failed, L"Switchy", MB_OK | MB_ICONWARNING);
    return rc == 0;
}

/* ---------- tray ---------- */

static HICON make_icon(COLORREF fill)
{
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB } };
    DWORD *px;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, NULL);
    HGDIOBJ old = SelectObject(dc, color);
    int r = cx / 4; /* corner radius */
    DWORD argb = 0xFF000000u | ((DWORD)GetRValue(fill) << 16) | ((DWORD)GetGValue(fill) << 8) | GetBValue(fill);
    for (int y = 0; y < cy; y++)
        for (int x = 0; x < cx; x++) {
            int dx = x < r ? r - x : x >= cx - r ? x - (cx - r - 1) : 0;
            int dy = y < r ? r - y : y >= cy - r ? y - (cy - r - 1) : 0;
            px[y * cx + x] = dx * dx + dy * dy <= r * r ? argb : 0;
        }
    HFONT font = CreateFontW(-cy * 3 / 4, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldf = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = { 0, 0, cx, cy };
    GdiFlush();
    DrawTextW(dc, L"S", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    for (int i = 0; i < cx * cy; i++) /* GDI clears alpha where it draws: text is inside the fill */
        if (px[i] && !(px[i] >> 24)) px[i] |= 0xFF000000u;
    SelectObject(dc, oldf);
    DeleteObject(font);
    SelectObject(dc, old);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    return icon;
}

static void tray(DWORD op)
{
    NOTIFYICONDATAW n = { sizeof(n) };
    n.hWnd = g.hwnd;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_APP_TRAY;
    n.hIcon = g.st.enabled ? g.icon_on : g.icon_off;
    wsprintfW(n.szTip, L"%s%s%s", g.st.enabled ? g.s->tip_on : g.s->tip_off,
              g.elevated ? g.s->tip_admin : L"", g.st.bypass ? g.s->tip_paused : L"");
    BOOL ok = Shell_NotifyIconW(op, &n);
    if (op == NIM_DELETE) g.tray_added = FALSE;
    else if (op == NIM_ADD) g.tray_added = ok;
    else if (!ok) g.tray_added = Shell_NotifyIconW(NIM_ADD, &n); /* icon lost: add it back */
}

/* At logon the taskbar may not exist yet: retry until it does (or TaskbarCreated arrives). */
static void update_tray(void)
{
    tray(g.tray_added ? NIM_MODIFY : NIM_ADD);
    if (!g.tray_added) SetTimer(g.hwnd, TIMER_TRAY, 2000, NULL);
}

/* ---------- full-screen games ---------- */

/* A game in front: exclusive full-screen (Windows reports it), or a window with
 * no title bar covering its whole monitor (borderless). A maximized window keeps
 * its title bar, and the desktop is not a game. Never called from the hook. */
static BOOL foreground_is_fullscreen(void)
{
    /* Only the exclusive D3D state: QUNS_BUSY can stay set for a full-screen video
     * on another monitor while you type here. Everything else is judged by the
     * foreground window and its own monitor. */
    QUERY_USER_NOTIFICATION_STATE q;
    if (SHQueryUserNotificationState(&q) == S_OK && q == QUNS_RUNNING_D3D_FULL_SCREEN) return TRUE;
    HWND w = GetForegroundWindow();
    if (!w || w == GetShellWindow() || w == GetDesktopWindow()) return FALSE;
    wchar_t cls[32];
    if (GetClassNameW(w, cls, 32) && (!lstrcmpW(cls, L"WorkerW") || !lstrcmpW(cls, L"Progman") ||
                                      !lstrcmpW(cls, L"Shell_TrayWnd")))
        return FALSE;
    if ((GetWindowLongW(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) return FALSE;
    RECT r;
    MONITORINFO mi = { sizeof(mi) };
    if (!GetWindowRect(w, &r) || !GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return FALSE;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

/* The hook only reads g.st.bypass; it is computed here, outside the hook. */
static void update_bypass(void)
{
    int b = g.skip_fullscreen && foreground_is_fullscreen();
    if (b != g.st.bypass) {
        g.st.bypass = b;
        update_tray();
    }
}

static void CALLBACK on_foreground(HWINEVENTHOOK h, DWORD ev, HWND w, LONG obj, LONG child, DWORD tid, DWORD ms)
{
    (void)h; (void)ev; (void)w; (void)obj; (void)child; (void)tid; (void)ms;
    update_bypass();
}

static BOOL take_mutex(void);

static BOOL mutex_owned_elsewhere(void)
{
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Switchy");
    if (m) { CloseHandle(m); return TRUE; }
    return GetLastError() == ERROR_ACCESS_DENIED; /* exists, owned by the elevated copy */
}

/* Start the elevated task now and step aside, instead of waiting for the next sign-in.
 * Returns TRUE if the elevated copy took over (this instance must exit). */
static BOOL handover_to_task(void)
{
    do_actions(sw_reset(&g.st));
    if (g.hook) { UnhookWindowsHookEx(g.hook); g.hook = NULL; }
    CloseHandle(g.mutex);
    g.mutex = NULL;
    lstrcpyW(buf, L"schtasks.exe /Run /TN \"" TASK_NAME L"\"");
    if (run_hidden(buf) == 0)
        for (int i = 0; i < 30; i++) {
            if (mutex_owned_elsewhere()) return TRUE;
            Sleep(100);
        }
    if (!take_mutex()) return TRUE; /* it showed up late: still exit, never run twice */
    install_hook();
    return FALSE;
}

static void show_menu(void)
{
    BOOL run = run_autostart_exists(), task = task_exists();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g.st.enabled ? MF_CHECKED : 0), ID_ENABLED, g.s->enabled);
    AppendMenuW(m, MF_STRING | (g.st.popup ? MF_CHECKED : 0), ID_POPUP, g.s->popup);
    AppendMenuW(m, MF_STRING | (g.skip_fullscreen ? MF_CHECKED : 0), ID_FULLSCREEN, g.s->fullscreen);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (run ? MF_CHECKED : 0), ID_AUTOSTART, g.s->autostart);
    AppendMenuW(m, MF_STRING | (task ? MF_CHECKED : 0), ID_AUTOSTART_ADMIN, g.s->autostart_admin);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, g.s->exit);
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g.hwnd);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, g.hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);

    switch (id) {
    case ID_ENABLED:
        g.st.enabled = !g.st.enabled;
        update_tray();
        break;
    case ID_FULLSCREEN:
        g.skip_fullscreen = !g.skip_fullscreen;
        cfg_set(L"SkipFullscreen", (DWORD)g.skip_fullscreen);
        update_bypass();
        break;
    case ID_POPUP:
        do_actions(sw_reset(&g.st));
        g.st.popup = !g.st.popup;
        cfg_set(L"Popup", (DWORD)g.st.popup);
        break;
    case ID_AUTOSTART:
        if (!run && task && !set_task_autostart(FALSE)) break;  /* one autostart at a time */
        set_run_autostart(!run);
        break;
    case ID_AUTOSTART_ADMIN:
        if (!set_task_autostart(!task) || task) break;
        set_run_autostart(FALSE);
        if (!g.elevated) {
            if (handover_to_task()) { log_exit(L"handed over to the admin task"); DestroyWindow(g.hwnd); break; }
            MessageBoxW(g.hwnd, g.s->admin_later, L"Switchy", MB_OK | MB_ICONINFORMATION);
        }
        break;
    case ID_EXIT:
        log_exit(L"menu Exit");
        DestroyWindow(g.hwnd);
        break;
    }
}

/* ---------- window ---------- */


static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_APP_ACT:
        do_actions((unsigned)wp);
        return 0;
    case WM_APP_ARM:
        SetTimer(h, TIMER_STALE, STALE_MS, NULL);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REHOOK) {
            install_hook();
        } else if (wp == TIMER_STALE) {
            do_actions(sw_tick(&g.st, GetTickCount()));
            if (!g.st.caps_held) { KillTimer(h, TIMER_STALE); g.stale_armed = FALSE; }
        } else if (wp == TIMER_TRAY) {
            KillTimer(h, TIMER_TRAY);
            update_tray();
        } else if (wp == TIMER_UNDO) {
            KillTimer(h, TIMER_UNDO);
            undo_switch();
        } else if (wp == TIMER_FULLSCREEN) {
            update_bypass();
        } else if (wp == TIMER_CAPSOFF) {
            KillTimer(h, TIMER_CAPSOFF);
            caps_off_with_shift();
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
            do_actions(sw_reset(&g.st));
            install_hook();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            do_actions(sw_reset(&g.st));
            install_hook();
        }
        return TRUE;
    case WM_APP_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_menu();
        else if (lp == WM_LBUTTONDBLCLK) { g.st.enabled = !g.st.enabled; update_tray(); }
        return 0;
    case WM_CLOSE:          /* only the tray menu may close Switchy, not other apps */
        log_exit(L"WM_CLOSE ignored");
        return 0;
    case WM_ENDSESSION:
        if (wp) log_exit(L"session end");
        return 0;
    case WM_DESTROY:
        do_actions(sw_reset(&g.st));
        tray(NIM_DELETE);
        WTSUnRegisterSessionNotification(h);
        if (g.fg_hook) UnhookWinEvent(g.fg_hook);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g.msg_taskbar && msg) { g.tray_added = FALSE; update_tray(); return 0; } /* Explorer restarted */
    return DefWindowProcW(h, msg, wp, lp);
}

/* Keep the hook path resident and on time: a hook that misses its deadline
 * (page fault under heavy disk load, low priority, EcoQoS) is removed by Windows. */
static void harden_process(void)
{
    HANDLE p = GetCurrentProcess();
    SetPriorityClass(p, ABOVE_NORMAL_PRIORITY_CLASS);
    SetProcessWorkingSetSizeEx(p, 4u << 20, 32u << 20, QUOTA_LIMITS_HARDWS_MIN_ENABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE);
    PROCESS_POWER_THROTTLING_STATE pt = { PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                          PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0 };
    SetProcessInformation(p, ProcessPowerThrottling, &pt, sizeof(pt));
}

/* Same name as the original Switchy: the old and new versions never run together. */
static BOOL take_mutex(void)
{
    g.mutex = CreateMutexW(NULL, FALSE, L"Switchy");
    DWORD e = GetLastError();
    if (g.mutex && e != ERROR_ALREADY_EXISTS) return TRUE;
    if (g.mutex) CloseHandle(g.mutex);
    g.mutex = NULL;
    return FALSE; /* ERROR_ALREADY_EXISTS, or ERROR_ACCESS_DENIED when an elevated copy owns it */
}

static int has_arg(int argc, wchar_t **argv, const wchar_t *a)
{
    for (int i = 1; i < argc; i++)
        if (lstrcmpiW(argv[i], a) == 0) return 1;
    return 0;
}

static int run(void)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    GetModuleFileNameW(NULL, g.self, MAX_PATH);
    g.elevated = is_elevated();
    g.s = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? &RU : &EN;

    if (argv && has_arg(argc, argv, L"--install-task")) return (int)install_task();
    if (argv && has_arg(argc, argv, L"--uninstall-task")) return (int)uninstall_task();

    if (!take_mutex()) return 0; /* already running (maybe elevated: then we get access denied) */

    /* "nopopup" kept for old shortcuts; otherwise the tray setting (default: pop-up on) */
    /* Default: Caps Lock does exactly what the Windows language hotkey does (silent).
     * The Win+Space pop-up is an opt-in; "nopopup" is kept for old shortcuts. */
    int popup = argv && has_arg(argc, argv, L"nopopup") ? 0 : (int)cfg_get(L"Popup", 0);
    g.skip_fullscreen = (BOOL)cfg_get(L"SkipFullscreen", 1);
    sw_init(&g.st, 1, popup);
    if (argv) LocalFree(argv);

    g.started = GetTickCount();
    harden_process();
    g.icon_on = make_icon(RGB(37, 99, 235));
    g.icon_off = make_icon(RGB(140, 140, 140));
    g.msg_taskbar = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SwitchyWindow";
    RegisterClassW(&wc);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Switchy", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!g.hwnd) return 1;
    WTSRegisterSessionNotification(g.hwnd, NOTIFY_FOR_THIS_SESSION);

    if (!install_hook()) {
        MessageBoxW(NULL, g.s->hook_error, L"Switchy", MB_OK | MB_ICONERROR);
        return 1;
    }
    SetTimer(g.hwnd, TIMER_REHOOK, REHOOK_MS, NULL);
    /* foreground changes right away, plus a 1 s re-check for games that go full-screen later */
    g.fg_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, on_foreground, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(g.hwnd, TIMER_FULLSCREEN, FULLSCREEN_MS, NULL);
    update_bypass();
    update_tray();

    MSG m;
    BOOL r;
    while ((r = GetMessageW(&m, NULL, 0, 0)) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (r < 0) log_exit(L"GetMessage failed");
    if (g.hook) UnhookWindowsHookEx(g.hook);
    return 0;
}

void WINAPI start(void) { ExitProcess((UINT)run()); }
