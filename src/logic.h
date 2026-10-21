/*
 * Switchy key logic: a pure state machine, no Windows headers.
 * The keyboard hook feeds it physical key events and gets back
 * "swallow this event?" plus a set of actions to run later,
 * outside the hook, so the hook itself returns in microseconds.
 */
#ifndef SWITCHY_LOGIC_H
#define SWITCHY_LOGIC_H

#define SW_VK_SHIFT    0x10
#define SW_VK_MENU     0x12
#define SW_VK_CAPITAL  0x14
#define SW_VK_LSHIFT   0xA0
#define SW_VK_RSHIFT   0xA1
#define SW_VK_LMENU    0xA4
#define SW_VK_RMENU    0xA5
#define SW_VK_LCONTROL 0xA2
#define SW_VK_LWIN     0x5B
#define SW_VK_SPACE    0x20
#define SW_VK_OEM_3    0xC0 /* grave accent */

/* Caps Lock is considered released if no event for it came for this long.
 * Holding a key produces auto-repeat events every ~30 ms after the initial
 * delay (at most 1 s), so a real hold never goes silent this long. */
#define SW_STALE_MS 1500u

enum {
    SW_ACT_SWITCH         = 1 << 0, /* switch to the next layout */
    SW_ACT_TOGGLE_CAPS    = 1 << 1, /* toggle the real Caps Lock state */
    SW_ACT_TOGGLE_ENABLED = 1 << 2, /* Switchy was enabled or disabled */
    SW_ACT_POPUP_BEGIN    = 1 << 3, /* press Win, tap Space: show the layout flyout */
    SW_ACT_POPUP_END      = 1 << 4, /* release Win (the flyout applies its choice) */
    SW_ACT_UNDO_SWITCH    = 1 << 5  /* go back to the previous layout (after POPUP_END) */
};

typedef enum { SW_MODE_IDLE, SW_MODE_SWITCH, SW_MODE_CAPS, SW_MODE_TOGGLE, SW_MODE_PASS } SwMode;

typedef struct {
    int enabled;     /* Caps Lock switches layouts */
    int bypass;      /* a full-screen app is in front: leave every key alone */
    int popup;       /* use the Win+Space flyout instead of a silent switch */
    int caps_held;   /* physical Caps Lock is down */
    unsigned shift_down; /* physical Shift held: bit 0 left, bit 1 right */
    unsigned alt_down;   /* physical Alt held: bit 0 left, bit 1 right */
    int popup_shown; /* this press opened the flyout (Win is held by us) */
    SwMode mode;     /* what the current Caps Lock press means */
    unsigned last_caps_ms;
} SwState;

typedef struct {
    int swallow;          /* block the key from reaching applications */
    unsigned actions;     /* SW_ACT_* bit set */
} SwResult;

void sw_init(SwState *s, int enabled, int popup);
/* Forget any key that is "down". Returns actions needed to undo half-done work
 * (a held Win key in popup mode). Call after unlock, resume, hook reinstall. */
unsigned sw_reset(SwState *s);
SwResult sw_on_key(SwState *s, unsigned vk, int down, unsigned now_ms);
/* Forget Shift/Alt sides that are actually up (bit 0 left, bit 1 right held):
 * their release can go where the hook doesn't see it (an admin window, the
 * secure desktop). Only ever clears: call before a Caps Lock press. */
void sw_sync_mods(SwState *s, unsigned shift_held, unsigned alt_held);

/* The Windows language hotkey ("Language Hotkey" in HKCU\Keyboard Layout\Toggle):
 * '1' or other = Alt+Shift, '2' = Ctrl+Shift, '3' = none set (Win+Space), '4' = grave. */
typedef struct { unsigned char vk, up; } SwKey;
/* Its modifier key (0 for grave): the caller says if the user already holds it. */
unsigned sw_hotkey_mod(int hotkey);
/* The keys that press the hotkey, into out[4]; returns how many. A modifier the
 * user already holds is borrowed, not pressed and released: our release would
 * cancel the user's own hold. */
int sw_hotkey_keys(int hotkey, int mod_held, SwKey *out);
/* Periodic check while Caps Lock is held: recovers if its release was lost
 * (secure desktop, hook removed mid-press). */
unsigned sw_tick(SwState *s, unsigned now_ms);

#endif
