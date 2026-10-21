#include "logic.h"

void sw_init(SwState *s, int enabled, int popup)
{
    s->enabled = enabled;
    s->bypass = 0;
    s->popup = popup;
    s->caps_held = 0;
    s->shift_down = 0;
    s->alt_down = 0;
    s->popup_shown = 0;
    s->mode = SW_MODE_IDLE;
    s->last_caps_ms = 0;
}

static unsigned end_press(SwState *s)
{
    unsigned a = 0;
    if (s->popup_shown) {
        a |= SW_ACT_POPUP_END;
        /* Shift or Alt came after the flyout opened: the press was not a switch */
        if (s->mode == SW_MODE_CAPS || s->mode == SW_MODE_TOGGLE)
            a |= SW_ACT_UNDO_SWITCH;
    }
    s->popup_shown = 0;
    s->caps_held = 0;
    s->mode = SW_MODE_IDLE;
    return a;
}

unsigned sw_reset(SwState *s)
{
    unsigned a = end_press(s);
    s->shift_down = 0;
    s->alt_down = 0;
    return a;
}

static SwResult caps_down(SwState *s, unsigned now)
{
    SwResult r = { 0, 0 };

    if (s->caps_held) { /* auto-repeat */
        s->last_caps_ms = now;
        r.swallow = s->mode != SW_MODE_PASS;
        return r;
    }
    s->caps_held = 1;
    s->last_caps_ms = now;

    if (s->bypass) {
        /* full-screen game: Caps Lock (and Alt/Shift+Caps Lock) belong to the game */
        s->mode = SW_MODE_PASS;
    } else if (s->alt_down) {
        s->enabled = !s->enabled;
        s->mode = SW_MODE_TOGGLE;
        r.swallow = 1;
        r.actions = SW_ACT_TOGGLE_ENABLED;
    } else if (!s->enabled) {
        s->mode = SW_MODE_PASS;
    } else if (s->shift_down) {
        s->mode = SW_MODE_CAPS;
        r.swallow = 1;
        r.actions = SW_ACT_TOGGLE_CAPS;
    } else {
        s->mode = SW_MODE_SWITCH;
        r.swallow = 1;
        if (s->popup) {
            s->popup_shown = 1;
            r.actions = SW_ACT_POPUP_BEGIN;
        }
    }
    return r;
}

static SwResult caps_up(SwState *s)
{
    SwResult r = { 0, 0 };

    if (!s->caps_held) /* release without a press we saw: let it through */
        return r;
    r.swallow = s->mode != SW_MODE_PASS;
    if (s->mode == SW_MODE_SWITCH && !s->popup_shown) /* the flyout already switched */
        r.actions |= SW_ACT_SWITCH;
    r.actions |= end_press(s);
    return r;
}

/* Each side separately: releasing one Shift while the other is held keeps Shift held. */
static unsigned side(unsigned held, unsigned vk, unsigned right_vk, int down)
{
    unsigned bit = vk == right_vk ? 2u : 1u;
    return down ? held | bit : held & ~bit;
}

void sw_sync_mods(SwState *s, unsigned shift_held, unsigned alt_held)
{
    s->shift_down &= shift_held;
    s->alt_down &= alt_held;
}

SwResult sw_on_key(SwState *s, unsigned vk, int down, unsigned now)
{
    SwResult r = { 0, 0 };

    switch (vk) {
    case SW_VK_CAPITAL:
        return down ? caps_down(s, now) : caps_up(s);

    case SW_VK_SHIFT:
    case SW_VK_LSHIFT:
    case SW_VK_RSHIFT:
        if (down && !s->shift_down && s->caps_held && s->mode == SW_MODE_SWITCH) {
            /* Caps Lock first, then Shift: same meaning as Shift+Caps Lock.
             * If the flyout is already open, the layout is undone on release. */
            s->mode = SW_MODE_CAPS;
            r.actions = SW_ACT_TOGGLE_CAPS;
        }
        s->shift_down = side(s->shift_down, vk, SW_VK_RSHIFT, down);
        return r; /* Shift itself is never blocked */

    case SW_VK_MENU:
    case SW_VK_LMENU:
    case SW_VK_RMENU:
        if (down && !s->alt_down && s->caps_held && s->mode == SW_MODE_SWITCH) {
            /* Caps Lock first, then Alt: same meaning as Alt+Caps Lock (no switch;
             * an already opened flyout is undone on release, like for Shift). */
            s->enabled = !s->enabled;
            s->mode = SW_MODE_TOGGLE;
            r.actions = SW_ACT_TOGGLE_ENABLED;
        }
        s->alt_down = side(s->alt_down, vk, SW_VK_RMENU, down);
        return r; /* Alt itself is never blocked */
    }
    return r;
}

unsigned sw_tick(SwState *s, unsigned now)
{
    if (s->caps_held && (unsigned)(now - s->last_caps_ms) > SW_STALE_MS)
        return end_press(s);
    return 0;
}
