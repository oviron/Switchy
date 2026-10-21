/* Unit tests for the key state machine. Plain C, runs on any OS:
 *   cc -std=c99 -Wall -Wextra -Isrc tests/test_logic.c src/logic.c -o test && ./test */
#include <stdio.h>
#include "logic.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static SwState st;
static unsigned t;

static SwResult key(unsigned vk, int down) { t += 10; return sw_on_key(&st, vk, down, t); }

static void test_plain_switch(void)
{
    sw_init(&st, 1, 0);
    SwResult d = key(SW_VK_CAPITAL, 1);
    CHECK(d.swallow && d.actions == 0);          /* nothing happens on press */
    SwResult u = key(SW_VK_CAPITAL, 0);
    CHECK(u.swallow && u.actions == SW_ACT_SWITCH);
    CHECK(st.mode == SW_MODE_IDLE && !st.caps_held);
}

static void test_autorepeat_switches_once(void)
{
    sw_init(&st, 1, 0);
    key(SW_VK_CAPITAL, 1);
    for (int i = 0; i < 20; i++) {
        SwResult r = key(SW_VK_CAPITAL, 1);
        CHECK(r.swallow && r.actions == 0);
    }
    CHECK(key(SW_VK_CAPITAL, 0).actions == SW_ACT_SWITCH);
}

static void test_shift_then_caps_toggles_capslock(void)
{
    sw_init(&st, 1, 0);
    CHECK(!key(SW_VK_LSHIFT, 1).swallow);
    SwResult d = key(SW_VK_CAPITAL, 1);
    CHECK(d.swallow && d.actions == SW_ACT_TOGGLE_CAPS);
    SwResult u = key(SW_VK_CAPITAL, 0);
    CHECK(u.swallow && u.actions == 0);           /* no layout switch */
    key(SW_VK_LSHIFT, 0);
}

static void test_right_shift_works_too(void)
{
    sw_init(&st, 1, 0);
    key(SW_VK_RSHIFT, 1);
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_TOGGLE_CAPS);
    key(SW_VK_CAPITAL, 0);
    key(SW_VK_RSHIFT, 0);
}

static void test_caps_then_shift_toggles_capslock(void)
{
    sw_init(&st, 1, 0);
    key(SW_VK_CAPITAL, 1);
    SwResult s = key(SW_VK_LSHIFT, 1);
    CHECK(!s.swallow && s.actions == SW_ACT_TOGGLE_CAPS);
    CHECK(key(SW_VK_LSHIFT, 1).actions == 0);     /* Shift auto-repeat: once only */
    CHECK(key(SW_VK_CAPITAL, 0).actions == 0);
    key(SW_VK_LSHIFT, 0);
}

static void test_alt_caps_disables_and_enables(void)
{
    sw_init(&st, 1, 0);
    key(SW_VK_LMENU, 1);
    SwResult d = key(SW_VK_CAPITAL, 1);
    CHECK(d.swallow && d.actions == SW_ACT_TOGGLE_ENABLED && !st.enabled);
    CHECK(key(SW_VK_CAPITAL, 0).swallow);
    key(SW_VK_LMENU, 0);

    /* disabled: Caps Lock passes through untouched */
    SwResult pd = key(SW_VK_CAPITAL, 1), pu = key(SW_VK_CAPITAL, 0);
    CHECK(!pd.swallow && pd.actions == 0 && !pu.swallow && pu.actions == 0);

    key(SW_VK_RMENU, 1);
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_TOGGLE_ENABLED && st.enabled);
    key(SW_VK_CAPITAL, 0);
    key(SW_VK_RMENU, 0);
    CHECK(key(SW_VK_CAPITAL, 1).swallow);
    CHECK(key(SW_VK_CAPITAL, 0).actions == SW_ACT_SWITCH);
}

static void test_caps_then_alt_toggles_enabled(void)
{
    sw_init(&st, 1, 0);
    key(SW_VK_CAPITAL, 1);
    SwResult a = key(SW_VK_LMENU, 1);
    CHECK(!a.swallow && a.actions == SW_ACT_TOGGLE_ENABLED && !st.enabled);
    CHECK(key(SW_VK_LMENU, 1).actions == 0);      /* Alt auto-repeat: once only */
    CHECK(key(SW_VK_CAPITAL, 0).actions == 0);    /* no layout switch */
    key(SW_VK_LMENU, 0);
    /* while off, Caps Lock is a plain Caps Lock: Caps-then-Alt must not re-enable */
    CHECK(!key(SW_VK_CAPITAL, 1).swallow);
    key(SW_VK_LMENU, 1);
    CHECK(!st.enabled);
    key(SW_VK_CAPITAL, 0);
    key(SW_VK_LMENU, 0);
    key(SW_VK_LMENU, 1);                          /* Alt first: back on */
    key(SW_VK_CAPITAL, 1);
    CHECK(st.enabled);
    key(SW_VK_CAPITAL, 0);
    key(SW_VK_LMENU, 0);
}

static void test_popup_caps_then_alt_undoes_flyout(void)
{
    sw_init(&st, 1, 1);
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_POPUP_BEGIN);
    CHECK(key(SW_VK_LMENU, 1).actions == SW_ACT_TOGGLE_ENABLED);
    CHECK(key(SW_VK_CAPITAL, 0).actions == (SW_ACT_POPUP_END | SW_ACT_UNDO_SWITCH));
    key(SW_VK_LMENU, 0);
}

/* Original bug: injected Alt+Shift (from the switch itself) looked like a held
 * Alt to the next Caps press, so fast double Caps disabled Switchy. Injected
 * keys never reach the state machine now, and only a physical Alt counts. */
static void test_fast_double_caps_never_disables(void)
{
    sw_init(&st, 1, 0);
    for (int i = 0; i < 50; i++) {
        CHECK(key(SW_VK_CAPITAL, 1).actions == 0);
        CHECK(key(SW_VK_CAPITAL, 0).actions == SW_ACT_SWITCH);
    }
    CHECK(st.enabled);
}

static void test_popup_toggle_mid_press_is_safe(void)
{
    sw_init(&st, 1, 1);
    key(SW_VK_CAPITAL, 1);                        /* flyout open, Win held */
    st.popup = 0;                                 /* user turns pop-up off from the tray */
    CHECK(key(SW_VK_CAPITAL, 0).actions & SW_ACT_POPUP_END); /* Win still released */
}

static void test_popup_mode(void)
{
    sw_init(&st, 1, 1);
    SwResult d = key(SW_VK_CAPITAL, 1);
    CHECK(d.swallow && d.actions == SW_ACT_POPUP_BEGIN);
    CHECK(key(SW_VK_CAPITAL, 1).actions == 0);
    SwResult u = key(SW_VK_CAPITAL, 0);
    CHECK(u.swallow && u.actions == SW_ACT_POPUP_END);
}

static void test_popup_caps_then_shift(void)
{
    sw_init(&st, 1, 1);
    key(SW_VK_CAPITAL, 1);
    CHECK(key(SW_VK_LSHIFT, 1).actions == SW_ACT_TOGGLE_CAPS);
    CHECK(key(SW_VK_CAPITAL, 0).actions == (SW_ACT_POPUP_END | SW_ACT_UNDO_SWITCH));
    key(SW_VK_LSHIFT, 0);
}

static void test_popup_shift_then_caps_no_flyout(void)
{
    sw_init(&st, 1, 1);
    key(SW_VK_LSHIFT, 1);
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_TOGGLE_CAPS);
    CHECK(key(SW_VK_CAPITAL, 0).actions == 0);    /* no flyout was opened: nothing to close */
    key(SW_VK_LSHIFT, 0);
}

static void test_lost_release_recovers(void)
{
    sw_init(&st, 1, 1);
    key(SW_VK_CAPITAL, 1);                        /* Win is now held by us */
    CHECK(sw_tick(&st, t + 100) == 0);            /* still fresh */
    CHECK(sw_tick(&st, t + SW_STALE_MS + 1) == SW_ACT_POPUP_END);
    CHECK(!st.caps_held);
    /* next press starts clean */
    t += SW_STALE_MS + 10;
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_POPUP_BEGIN);
    key(SW_VK_CAPITAL, 0);
}

static void test_tick_wraparound(void)
{
    sw_init(&st, 1, 0);
    t = 0xFFFFFFF0u;                              /* GetTickCount wraps after 49.7 days */
    key(SW_VK_CAPITAL, 1);
    CHECK(sw_tick(&st, t + 20) == 0 && st.caps_held);
    key(SW_VK_CAPITAL, 0);
    t = 0;
}

static void test_release_without_press_passes(void)
{
    sw_init(&st, 1, 0);
    SwResult u = key(SW_VK_CAPITAL, 0);
    CHECK(!u.swallow && u.actions == 0);
}

static void test_reset_clears_modifiers(void)
{
    sw_init(&st, 1, 1);
    key(SW_VK_LMENU, 1);
    key(SW_VK_LSHIFT, 1);
    sw_reset(&st);                                /* e.g. Alt release lost on lock screen */
    CHECK(key(SW_VK_CAPITAL, 1).actions == SW_ACT_POPUP_BEGIN && st.enabled);
    CHECK(sw_reset(&st) == SW_ACT_POPUP_END);     /* reset mid-press releases Win */
}

static void test_other_keys_ignored(void)
{
    sw_init(&st, 1, 0);
    for (unsigned vk = 1; vk < 256; vk++) {
        if (vk == SW_VK_CAPITAL || vk == SW_VK_SHIFT || vk == SW_VK_LSHIFT || vk == SW_VK_RSHIFT ||
            vk == SW_VK_MENU || vk == SW_VK_LMENU || vk == SW_VK_RMENU)
            continue;
        SwResult a = key(vk, 1), b = key(vk, 0);
        CHECK(!a.swallow && !a.actions && !b.swallow && !b.actions);
    }
}

int main(void)
{
    test_plain_switch();
    test_autorepeat_switches_once();
    test_shift_then_caps_toggles_capslock();
    test_right_shift_works_too();
    test_caps_then_shift_toggles_capslock();
    test_alt_caps_disables_and_enables();
    test_caps_then_alt_toggles_enabled();
    test_popup_caps_then_alt_undoes_flyout();
    test_fast_double_caps_never_disables();
    test_popup_mode();
    test_popup_toggle_mid_press_is_safe();
    test_popup_caps_then_shift();
    test_popup_shift_then_caps_no_flyout();
    test_lost_release_recovers();
    test_tick_wraparound();
    test_release_without_press_passes();
    test_reset_clears_modifiers();
    test_other_keys_ignored();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
