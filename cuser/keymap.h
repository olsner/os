// US layout, scan code set 1 (XT set) -> ASCII table
// positive values are ascii key codes, negative values are some kind of special
// key. zeroes are ignored.

#define SPEC_IGNORE "\x00"
#define SPEC_SHIFT "\xff"
// Ctrl and Alt keys are ignored for now
#define SPEC_CTRL SPEC_IGNORE
#define SPEC_ALT SPEC_IGNORE
#define SPEC_BS "\x08"
#define SPEC_ESC "\x1b"
#define SPEC_TAB "\x09"
#define SPEC_ENTER "\x0a"

static const char keymap[] = 
    // 0: dummy
    SPEC_IGNORE
    // 1..e: First row
    SPEC_ESC "1234567890-=" SPEC_BS
    // f..1c: Second row
    SPEC_TAB "qwertyuiop[]" SPEC_ENTER
    // 1d..28: Third row
    SPEC_CTRL "asdfghjkl;'"
    // 29:
    "`"
    // 2a..36: Fourth row
    SPEC_SHIFT "\\zxcvbnm,./" SPEC_SHIFT
    // 37, 38, 39: keypad *, left alt, space
    "*" SPEC_ALT " "
    // 3a: CapsLock
    // 2b..44: F1..F10
    // 46,46: NumLock, ScrollLock
    // 47..53: keypad: 789-456+1230.
    // 57, 58: F11, F12
;

static const char shifted_keymap[] = 
    SPEC_IGNORE
    SPEC_ESC "!@#$%^&*()_+" SPEC_BS
    SPEC_TAB "QWERTYUIOP{}" SPEC_ENTER
    SPEC_CTRL "ASDFGHJKL:\"`"
    SPEC_SHIFT "|ZXCVBNM<>?" SPEC_SHIFT
    "*" SPEC_ALT " ";
