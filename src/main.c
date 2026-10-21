/*
 * Switchy - switch keyboard layout with Caps Lock.
 * Windows layer: keyboard hook, tray icon, autostart. Built without the C runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <wtsapi32.h>
#include "logic.h"

#define SID_CHARS    192                        /* longest SID string, with room to spare */
extern IMAGE_DOS_HEADER __ImageBase;            /* this module: no GetModuleHandle import */
#define HINST        ((HINSTANCE)&__ImageBase)

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
#define TIMER_WINUP  7                          /* retry a Win release that Windows blocked */
#define WINUP_MS     500
#define WINUP_TRIES  20
#define TIMER_UNDOCHK 8                         /* did the direct undo take? */
#define WM_HK_INSTALL (WM_APP + 10)             /* to the hook thread */
#define WM_HK_REMOVE  (WM_APP + 11)
#define READY_NAME   L"SwitchyReady"
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
    DWORD hk_tid;      /* the hook thread */
    HANDLE hk_done;    /* it set this: started, or did what hook_call() asked */
    BOOL hk_ok;
    SRWLOCK lock;      /* st, gen, stale_armed: shared by the hook thread and the main one */
    HANDLE ready;      /* "SwitchyReady" event: exists once this copy has its hook */
    BOOL stale_armed, tray_added, elevated, skip_fullscreen, hook_off;
    unsigned gen;      /* bumped by every reset: actions the hook posted before it are stale */
    int busy;          /* waiting for a child process: no nested tray menu */
    BOOL win_borrowed; /* pop-up press: the user held Win already, we didn't press it */
    BOOL popup_ok;     /* pop-up press: the flyout keys went out (else nothing to undo) */
    BOOL undo_pending;
    HWND undo_wnd;     /* window and layout from before an undone pop-up press */
    HKL undo_hkl;
    int winup_tries, undo_waits;
    HWINEVENTHOOK fg_hook;
    DWORD started;
    const Strings *s;
    wchar_t self[MAX_PATH];
} g;

static wchar_t buf[4096]; /* scratch for paths, command lines, task XML */

/* ---------- keyboard ---------- */

/* Returns how many keys Windows took: 0 when blocked (UIPI, secure desktop). */
static UINT send_keys(const SwKey *k, int n)
{
    INPUT in[4];
    for (int i = 0; i < n; i++) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = k[i].vk;
        in[i].ki.wScan = (WORD)MapVirtualKeyW(k[i].vk, MAPVK_VK_TO_VSC);
        in[i].ki.dwFlags = (k[i].up ? KEYEVENTF_KEYUP : 0) | (k[i].vk == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0);
        in[i].ki.time = 0;
        in[i].ki.dwExtraInfo = TAG;
    }
    return SendInput((UINT)n, in, sizeof(INPUT)); /* one call: a real key can't land in the middle */
}

static unsigned held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void lock(void) { AcquireSRWLockExclusive(&g.lock); }
static void unlock(void) { ReleaseSRWLockExclusive(&g.lock); }

/* Press the language hotkey configured in Windows (Settings > Time & language >
 * Typing > Advanced keyboard settings > Input language hot keys). */
static void switch_layout(void)
{
    wchar_t v[4] = L"1";
    DWORD size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Keyboard Layout\\Toggle", L"Language Hotkey", RRF_RT_REG_SZ, NULL, v, &size);
    unsigned mod = sw_hotkey_mod(v[0]);
    SwKey k[4];
    send_keys(k, sw_hotkey_keys(v[0], mod && held((int)mod), k));
}

static void update_tray(void);

static void do_actions(unsigned a)
{
    if (a & SW_ACT_TOGGLE_ENABLED) update_tray();
    if (a & SW_ACT_POPUP_BEGIN) {
        /* Win already held by the user: borrow it, the flyout closes when they let go */
        static const SwKey k[] = { { VK_LWIN, 0 }, { VK_SPACE, 0 }, { VK_SPACE, 1 } };
        g.win_borrowed = held(VK_LWIN);
        if (!g.undo_pending) { /* the layout to come back to if this press turns out not to switch */
            g.undo_wnd = GetForegroundWindow();
            g.undo_hkl = GetKeyboardLayout(GetWindowThreadProcessId(g.undo_wnd, NULL));
        }
        int n = g.win_borrowed ? 2 : 3;
        g.popup_ok = send_keys(k + 3 - n, n) == (UINT)n;
    }
    if (a & SW_ACT_TOGGLE_CAPS) {
        static const SwKey k[] = { { VK_CAPITAL, 0 }, { VK_CAPITAL, 1 } };
        BOOL was_on = GetKeyState(VK_CAPITAL) & 1;
        send_keys(k, 2);
        /* With "Turn off Caps Lock: press Shift" the Caps Lock key can only turn it on */
        if (was_on) SetTimer(g.hwnd, TIMER_CAPSOFF, CAPSOFF_MS, NULL);
    }
    if (a & SW_ACT_SWITCH) switch_layout();
    if ((a & SW_ACT_POPUP_END) && !g.win_borrowed) {
        static const SwKey k[] = { { VK_LWIN, 1 } };
        /* blocked (an admin window took the focus): keep trying, a held Win breaks the arrows */
        if (!send_keys(k, 1)) { g.winup_tries = WINUP_TRIES; SetTimer(g.hwnd, TIMER_WINUP, WINUP_MS, NULL); }
    }
    /* the flyout applies its choice asynchronously after Win is released: undo a bit
     * later. Nothing to undo if the flyout never opened. */
    if ((a & SW_ACT_UNDO_SWITCH) && g.popup_ok) {
        g.undo_pending = TRUE;
        g.undo_waits = 0;
        SetTimer(g.hwnd, TIMER_UNDO, UNDO_DELAY_MS, NULL);
    }
}

/* Forget held keys (unlock, resume, ...). A Win press the hook posted before this
 * must not run after it: the reset already released Win and nothing would again. */
static void reset_keys(void)
{
    lock();
    g.gen++;
    unsigned a = sw_reset(&g.st);
    unlock();
    do_actions(a);
}

/* Caps Lock is still on after we tried to turn it off: Windows is set to turn it
 * off with Shift (erryox/Switchy#5). Tap the Shift the user is NOT holding, so
 * our key-up doesn't cancel the user's own Shift (both held: the caller waits). */
static void caps_off_with_shift(void)
{
    if (!(GetKeyState(VK_CAPITAL) & 1)) return;
    SwKey k[2] = { { VK_LSHIFT, 0 }, { VK_LSHIFT, 1 } };
    if (held(VK_LSHIFT)) k[0].vk = k[1].vk = VK_RSHIFT;
    send_keys(k, 2);
}

/* Back to the layout from before the press: ask its window for it directly. If
 * that didn't take (an admin window, an app that ignores the request), the old
 * way: forward through all the other layouts. */
static void undo_switch(BOOL check)
{
    HWND w = GetForegroundWindow();
    HKL now = GetKeyboardLayout(GetWindowThreadProcessId(w, NULL));
    if (!check && w && w == g.undo_wnd && g.undo_hkl &&
        PostMessageW(w, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)g.undo_hkl)) {
        SetTimer(g.hwnd, TIMER_UNDOCHK, UNDO_DELAY_MS, NULL);
        return;
    }
    g.undo_pending = FALSE;
    if (check && (w != g.undo_wnd || now == g.undo_hkl)) return;
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
            int down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            lock();
            /* A Shift/Alt release that went to an admin window or the secure desktop
             * never reached us: drop what Windows says is up before Caps Lock reads it. */
            if (k->vkCode == VK_CAPITAL && down && !g.st.caps_held)
                sw_sync_mods(&g.st, held(VK_LSHIFT) | held(VK_RSHIFT) << 1, held(VK_LMENU) | held(VK_RMENU) << 1);
            SwResult r = sw_on_key(&g.st, k->vkCode, down, k->time);
            unsigned gen = g.gen;
            BOOL arm = g.st.caps_held && !g.stale_armed;
            if (arm) g.stale_armed = TRUE;
            unlock();
            if (r.actions) PostMessageW(g.hwnd, WM_APP_ACT, r.actions, gen);
            if (arm) PostMessageW(g.hwnd, WM_APP_ARM, 0, 0);
            if (r.swallow) return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* Install the new hook first, then drop the old one: no gap, no double delivery
 * (both calls run back to back on this thread, so no event is dispatched between). */
static BOOL install_hook(void)
{
    if (g.hook_off) return FALSE; /* handing over to the admin task: it hooks, we must not */
    HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, hook_proc, HINST, 0);
    if (!h) return FALSE;
    if (g.hook) UnhookWindowsHookEx(g.hook);
    g.hook = h;
    return TRUE;
}

/* The hook has a thread of its own that does nothing else, as Microsoft advises:
 * Windows calls the hook on the thread that set it, and drops a hook that answers
 * late. The tray (Explorer busy at sign-in), menus and message boxes run on the
 * main thread and can no longer hold up every key press in the session. */
static DWORD WINAPI hook_thread(void *unused)
{
    (void)unused;
    MSG m;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE);   /* the queue exists from now on */
    SetTimer(NULL, 0, REHOOK_MS, NULL);          /* Windows drops slow hooks silently: re-install */
    SetEvent(g.hk_done);
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_HK_REMOVE) {
            g.hook_off = TRUE;
            if (g.hook) { UnhookWindowsHookEx(g.hook); g.hook = NULL; }
        } else {                                 /* WM_HK_INSTALL (wParam: also after a remove), WM_TIMER */
            if (m.message == WM_HK_INSTALL && m.wParam) g.hook_off = FALSE;
            g.hk_ok = install_hook();
        }
        if (m.lParam == 1) SetEvent(g.hk_done); /* hook_call() waits (a timer's lParam is 0) */
    }
    return 0;
}

/* Have the hook thread install (force: also after a remove) or remove the hook. */
static BOOL hook_call(UINT msg, BOOL force, BOOL wait)
{
    if (!PostThreadMessageW(g.hk_tid, msg, (WPARAM)force, wait)) return FALSE;
    if (wait) WaitForSingleObject(g.hk_done, INFINITE);
    return g.hk_ok;
}

/* ---------- process helpers ---------- */

/* Wait for a handle while still pumping messages, so the tray icon and Windows'
 * notifications (sent, not posted) don't stall meanwhile. */
static DWORD wait_pumping(HANDLE h)
{
    g.busy++;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &h, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1) break;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    g.busy--;
    DWORD code = 1;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return code;
}

/* Run System32\schtasks.exe by full path: with a bare "schtasks.exe" CreateProcess
 * looks in our own folder first, and the elevated helper may run from Downloads. */
static DWORD schtasks(const wchar_t *args)
{
    static wchar_t exe[MAX_PATH];
    UINT n = GetSystemDirectoryW(exe, MAX_PATH - 16);
    if (!n || n >= MAX_PATH - 16) return 1;
    lstrcatW(exe, L"\\schtasks.exe");
    wsprintfW(buf, L"\"%s\" %s", exe, args);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 1;
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
    DWORD n = ExpandEnvironmentStringsW(L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\", buf, MAX_PATH);
    return n && n <= MAX_PATH && lstrcatW(buf, file) != NULL; /* too long: buf holds no path */
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

/* Both ways the old shell:startup copy goes: it would start the old version too
 * (only once the Run entry is in place, so a failed write leaves the old one). */
static BOOL set_run_autostart(BOOL on)
{
    if (on) {
        wsprintfW(buf, L"\"%s\"", g.self);
        if (RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"Switchy", REG_SZ, buf,
                            (DWORD)(lstrlenW(buf) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) return FALSE;
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"Switchy");
    }
    remove_legacy_startup();
    return TRUE;
}

static BOOL task_exists(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TASK_TREE, 0, KEY_READ, &k) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(k);
    return TRUE;
}

/* Put this exe at target without ever leaving target missing: stage a copy next to
 * it, then swap. A running copy can be renamed, not replaced: it goes to .old. */
static BOOL place_exe(const wchar_t *target)
{
    static wchar_t staged[MAX_PATH + 4], old[MAX_PATH + 4];
    if (lstrcmpiW(target, g.self) == 0) return TRUE;
    wsprintfW(staged, L"%s.new", target);
    wsprintfW(old, L"%s.old", target);
    if (!CopyFileW(g.self, staged, FALSE)) return FALSE;
    if (MoveFileExW(staged, target, MOVEFILE_REPLACE_EXISTING)) return TRUE;
    DeleteFileW(old);
    if (MoveFileW(target, old)) {
        if (MoveFileW(staged, target)) { MoveFileExW(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); return TRUE; }
        MoveFileW(old, target);
    }
    DeleteFileW(staged);
    return FALSE;
}

/* The account as a SID: no name to escape, and not taken from the environment. */
static BOOL user_sid(wchar_t *out, int cap)
{
    union { TOKEN_USER u; BYTE b[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]; } tu;
    HANDLE t; DWORD n; wchar_t *s; BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return FALSE;
    if (GetTokenInformation(t, TokenUser, &tu, sizeof(tu), &n) && ConvertSidToStringSidW(tu.u.User.Sid, &s)) {
        if ((ok = lstrlenW(s) < cap)) lstrcpyW(out, s);
        LocalFree(s);
    }
    CloseHandle(t);
    return ok;
}

/* The XML template is ASCII: kept as 8-bit text in the exe (half the size), written as UTF-16. */
static void cat_ascii(const char *s)
{
    wchar_t *p = buf + lstrlenW(buf);
    while ((*p++ = (unsigned char)*s++) != 0) {}
}

static void xml_append(const wchar_t *s)
{
    wchar_t *p = buf + lstrlenW(buf);
    for (; *s; s++) {
        const wchar_t *e = *s == L'&' ? L"&amp;" : *s == L'<' ? L"&lt;" : *s == L'>' ? L"&gt;" : NULL;
        if (e) { lstrcpyW(p, e); p += lstrlenW(e); } else *p++ = *s;
    }
    *p = 0;
}

/* Elevated helper: copy the exe to Program Files (an elevated task must not run a
 * file the user can overwrite) and register a logon task with highest privileges.
 * Paths come from HKLM, not from the environment, and the task XML is written into
 * that admin-only folder, not %TEMP%, where a normal process could swap it. */
static DWORD install_task(void)
{
    static wchar_t dir[MAX_PATH], target[MAX_PATH], xml_path[MAX_PATH], sid[SID_CHARS], args[MAX_PATH + 64];
    DWORD size = sizeof(dir) - 32 * sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion", L"ProgramFilesDir",
                     RRF_RT_REG_SZ, NULL, dir, &size) != ERROR_SUCCESS) return 2;
    lstrcatW(dir, L"\\Switchy");
    CreateDirectoryW(dir, NULL);
    wsprintfW(target, L"%s\\Switchy.exe", dir);
    wsprintfW(xml_path, L"%s\\task.xml", dir);
    if (!place_exe(target)) return 3;
    if (!user_sid(sid, ARRAYSIZE(sid))) return 4;

    buf[0] = 0;
    cat_ascii("<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        "<RegistrationInfo><Description>Switchy: switch keyboard layout with Caps Lock</Description></RegistrationInfo>"
        "<Triggers><LogonTrigger><UserId>");
    lstrcatW(buf, sid);
    cat_ascii("</UserId></LogonTrigger></Triggers><Principals><Principal id=\"Author\"><UserId>");
    lstrcatW(buf, sid);
    cat_ascii("</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        "<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority></Settings>"
        "<Actions Context=\"Author\"><Exec><Command>\"");
    xml_append(target);
    cat_ascii("\"</Command></Exec></Actions></Task>");

    HANDLE f = CreateFileW(xml_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return 5;
    DWORD w, len = (DWORD)lstrlenW(buf) * 2; WORD bom = 0xFEFF;
    BOOL ok = WriteFile(f, &bom, 2, &w, NULL) && w == 2 && WriteFile(f, buf, len, &w, NULL) && w == len;
    CloseHandle(f);
    DWORD rc = 5;
    if (ok) {
        wsprintfW(args, L"/Create /TN \"" TASK_NAME L"\" /XML \"%s\" /F", xml_path);
        rc = schtasks(args);
    }
    DeleteFileW(xml_path);
    return rc;
}

static DWORD uninstall_task(void)
{
    return schtasks(L"/Delete /TN \"" TASK_NAME L"\" /F");
}

static BOOL set_task_autostart(BOOL on)
{
    DWORD rc = g.elevated ? (on ? install_task() : uninstall_task())
                          : run_self_elevated(on ? L"--install-task" : L"--uninstall-task");
    if (rc != 0) MessageBoxW(g.hwnd, g.s->admin_failed, L"Switchy", MB_OK | MB_ICONWARNING);
    return rc == 0;
}

/* ---------- tray ---------- */

static HICON make_icon(COLORREF fill, int cx, int cy)
{
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB } };
    DWORD *px = NULL;
    HICON icon = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, NULL);
    if (!dc || !color || !px || !mask) goto done; /* out of GDI resources: no icon, no crash */
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
    icon = CreateIconIndirect(&ii);
done:
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    if (dc) DeleteDC(dc);
    return icon;
}

/* Icons sized for the taskbar's current DPI: it can change without a sign-out
 * (Explorer then re-sends TaskbarCreated). Before the taskbar exists: system DPI. */
static void make_icons(void)
{
    HWND bar = FindWindowW(L"Shell_TrayWnd", NULL);
    UINT dpi = bar ? GetDpiForWindow(bar) : 0;
    int cx = dpi ? GetSystemMetricsForDpi(SM_CXSMICON, dpi) : GetSystemMetrics(SM_CXSMICON);
    int cy = dpi ? GetSystemMetricsForDpi(SM_CYSMICON, dpi) : GetSystemMetrics(SM_CYSMICON);
    if (g.icon_on) DestroyIcon(g.icon_on);
    if (g.icon_off) DestroyIcon(g.icon_off);
    g.icon_on = make_icon(RGB(37, 99, 235), cx, cy);
    g.icon_off = make_icon(RGB(140, 140, 140), cx, cy);
}

static void tray(DWORD op)
{
    NOTIFYICONDATAW n = { sizeof(n) };
    n.hWnd = g.hwnd;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_APP_TRAY;
    lock();
    int on = g.st.enabled, paused = g.st.bypass;
    unlock();
    n.hIcon = on ? g.icon_on : g.icon_off;
    wsprintfW(n.szTip, L"%s%s%s", on ? g.s->tip_on : g.s->tip_off,
              g.elevated ? g.s->tip_admin : L"", paused ? g.s->tip_paused : L"");
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
    lock();
    BOOL changed = b != g.st.bypass;
    g.st.bypass = b;
    unlock();
    if (changed) update_tray();
}

static void CALLBACK on_foreground(HWINEVENTHOOK h, DWORD ev, HWND w, LONG obj, LONG child, DWORD tid, DWORD ms)
{
    (void)h; (void)ev; (void)w; (void)obj; (void)child; (void)tid; (void)ms;
    update_bypass();
}

/* Foreground changes right away, plus a 1 s re-check for games that go full-screen
 * later. Only while the setting is on: off, nothing wakes us up for it. */
static void watch_fullscreen(void)
{
    if (g.skip_fullscreen) {
        if (!g.fg_hook)
            g.fg_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, on_foreground, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        SetTimer(g.hwnd, TIMER_FULLSCREEN, FULLSCREEN_MS, NULL);
    } else {
        if (g.fg_hook) { UnhookWinEvent(g.fg_hook); g.fg_hook = NULL; }
        KillTimer(g.hwnd, TIMER_FULLSCREEN);
    }
    update_bypass();
}

static BOOL take_mutex(void);

/* The other copy is up and hooked, not merely started (it can still fail before that). */
static BOOL ready_elsewhere(void)
{
    HANDLE e = OpenEventW(SYNCHRONIZE, FALSE, READY_NAME);
    if (e) { CloseHandle(e); return TRUE; }
    return GetLastError() == ERROR_ACCESS_DENIED; /* exists, made by the elevated copy */
}

/* Start the elevated task now and step aside, instead of waiting for the next sign-in.
 * Returns TRUE if the elevated copy took over (this instance must exit). */
static BOOL handover_to_task(void)
{
    reset_keys();
    hook_call(WM_HK_REMOVE, FALSE, TRUE); /* and no re-hook timer brings it back */
    CloseHandle(g.mutex);
    g.mutex = NULL;
    CloseHandle(g.ready);
    g.ready = NULL;
    if (schtasks(L"/Run /TN \"" TASK_NAME L"\"") == 0)
        for (int i = 0; i < 50; i++) {
            if (ready_elsewhere()) return TRUE;
            Sleep(100);
        }
    if (!take_mutex()) return TRUE; /* still starting: exit anyway, never run twice */
    hook_call(WM_HK_INSTALL, TRUE, TRUE);
    g.ready = CreateEventW(NULL, TRUE, TRUE, READY_NAME);
    return FALSE;
}

static void toggle_enabled(void)
{
    lock();
    g.st.enabled = !g.st.enabled;
    unlock();
    update_tray();
}

static void show_menu(void)
{
    BOOL run = run_autostart_exists(), task = task_exists();
    lock();
    int on = g.st.enabled, popup = g.st.popup;
    unlock();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (on ? MF_CHECKED : 0), ID_ENABLED, g.s->enabled);
    AppendMenuW(m, MF_STRING | (popup ? MF_CHECKED : 0), ID_POPUP, g.s->popup);
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
        toggle_enabled();
        break;
    case ID_FULLSCREEN:
        g.skip_fullscreen = !g.skip_fullscreen;
        cfg_set(L"SkipFullscreen", (DWORD)g.skip_fullscreen);
        watch_fullscreen();
        break;
    case ID_POPUP:
        reset_keys();
        lock();
        g.st.popup = popup = !popup;
        unlock();
        cfg_set(L"Popup", (DWORD)popup);
        break;
    case ID_AUTOSTART:
        if (!run && task) {   /* one autostart at a time: the new one first, then the old one goes */
            if (set_run_autostart(TRUE) && !set_task_autostart(FALSE)) set_run_autostart(FALSE);
            break;
        }
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

/* wtsapi32 is not a KnownDLL: load it from System32 by hand instead of importing it. */
static void session_notify(BOOL on)
{
    typedef BOOL (WINAPI *Reg)(HWND, DWORD);
    typedef BOOL (WINAPI *Unreg)(HWND);
    HMODULE m = LoadLibraryExW(L"wtsapi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) return;
    if (on) {
        Reg f = (Reg)(void (*)(void))GetProcAddress(m, "WTSRegisterSessionNotification");
        if (f) f(g.hwnd, NOTIFY_FOR_THIS_SESSION);
    } else {
        Unreg f = (Unreg)(void (*)(void))GetProcAddress(m, "WTSUnRegisterSessionNotification");
        if (f) f(g.hwnd);
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_APP_ACT:
        do_actions((unsigned)lp == g.gen ? (unsigned)wp : (unsigned)wp & ~SW_ACT_POPUP_BEGIN);
        return 0;
    case WM_APP_ARM:
        SetTimer(h, TIMER_STALE, STALE_MS, NULL);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_STALE) {
            lock();
            unsigned a = sw_tick(&g.st, GetTickCount());
            BOOL still = g.st.caps_held;
            if (!still) g.stale_armed = FALSE;
            unlock();
            do_actions(a);
            if (!still) KillTimer(h, TIMER_STALE);
        } else if (wp == TIMER_TRAY) {
            KillTimer(h, TIMER_TRAY);
            update_tray();
        } else if (wp == TIMER_UNDO) {
            if (held(VK_LWIN) && ++g.undo_waits < 100) return 0; /* the flyout applies its choice once Win is up */
            KillTimer(h, TIMER_UNDO);
            undo_switch(FALSE);
        } else if (wp == TIMER_UNDOCHK) {
            KillTimer(h, TIMER_UNDOCHK);
            undo_switch(TRUE);
        } else if (wp == TIMER_FULLSCREEN) {
            update_bypass();
        } else if (wp == TIMER_CAPSOFF) {
            if (held(VK_LSHIFT) && held(VK_RSHIFT)) return 0; /* no free Shift to tap: wait */
            KillTimer(h, TIMER_CAPSOFF);
            caps_off_with_shift();
        } else if (wp == TIMER_WINUP) {
            static const SwKey k[] = { { VK_LWIN, 1 } };
            if (send_keys(k, 1) || --g.winup_tries <= 0) KillTimer(h, TIMER_WINUP);
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
            reset_keys();
            hook_call(WM_HK_INSTALL, FALSE, FALSE);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            reset_keys();
            hook_call(WM_HK_INSTALL, FALSE, FALSE);
        }
        return TRUE;
    case WM_APP_TRAY:
        if ((lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) && !g.busy) show_menu();
        else if (lp == WM_LBUTTONDBLCLK) toggle_enabled();
        return 0;
    case WM_CLOSE:          /* only the tray menu may close Switchy, not other apps */
        log_exit(L"WM_CLOSE ignored");
        return 0;
    case WM_ENDSESSION:
        if (wp) log_exit(L"session end");
        return 0;
    case WM_DESTROY:
        reset_keys();
        tray(NIM_DELETE);
        session_notify(FALSE);
        if (g.fg_hook) UnhookWinEvent(g.fg_hook);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g.msg_taskbar && msg) { /* Explorer restarted, or the taskbar DPI changed */
        tray(NIM_DELETE);                 /* the icon may still be there: a plain add would fail forever */
        make_icons();
        update_tray();
        return 0;
    }
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
    DWORD len = GetModuleFileNameW(NULL, g.self, MAX_PATH);
    if (!len || len >= MAX_PATH) return 1; /* cut short: autostart would point elsewhere */
    g.elevated = is_elevated();
    g.s = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? &RU : &EN;

    if (argv && has_arg(argc, argv, L"--install-task")) return (int)install_task();
    if (argv && has_arg(argc, argv, L"--uninstall-task")) return (int)uninstall_task();

    if (!take_mutex()) return 0; /* already running (maybe elevated: then we get access denied) */

    /* Default: Caps Lock does exactly what the Windows language hotkey does (silent).
     * The Win+Space pop-up is an opt-in; "nopopup" is kept for old shortcuts. */
    int popup = argv && has_arg(argc, argv, L"nopopup") ? 0 : (int)cfg_get(L"Popup", 0);
    g.skip_fullscreen = (BOOL)cfg_get(L"SkipFullscreen", 1);
    sw_init(&g.st, 1, popup);
    if (argv) LocalFree(argv);

    g.started = GetTickCount();
    harden_process();
    make_icons();
    g.msg_taskbar = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = HINST;
    wc.lpszClassName = L"SwitchyWindow";
    RegisterClassW(&wc);
    g.hwnd = CreateWindowExW(0, L"SwitchyWindow", L"Switchy", 0, 0, 0, 0, 0, NULL, NULL, HINST, NULL);
    if (!g.hwnd) return 1;
    /* elevated, Explorer's message would be filtered out (UIPI): the icon never came back */
    if (g.msg_taskbar) ChangeWindowMessageFilterEx(g.hwnd, g.msg_taskbar, MSGFLT_ALLOW, NULL);
    session_notify(TRUE);

    HANDLE t = NULL;
    if ((g.hk_done = CreateEventW(NULL, FALSE, FALSE, NULL)) != NULL &&
        (t = CreateThread(NULL, 0, hook_thread, NULL, 0, &g.hk_tid)) != NULL)
        WaitForSingleObject(g.hk_done, INFINITE);
    if (!t || !hook_call(WM_HK_INSTALL, TRUE, TRUE)) {
        MessageBoxW(NULL, g.s->hook_error, L"Switchy", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(t);
    g.ready = CreateEventW(NULL, TRUE, TRUE, READY_NAME); /* for a copy handing over to us */
    watch_fullscreen();
    update_tray();

    MSG m;
    BOOL r;
    while ((r = GetMessageW(&m, NULL, 0, 0)) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (r < 0) log_exit(L"GetMessage failed");
    hook_call(WM_HK_REMOVE, FALSE, TRUE);
    return 0;
}

/* System DLLs from System32 only: the elevated helper may run from Downloads, next
 * to whatever was downloaded there. Static imports are all KnownDLLs. */
void WINAPI start(void)
{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    ExitProcess((UINT)run());
}
