#ifndef BLIT_EVDEV_CHAR_H
#define BLIT_EVDEV_CHAR_H

#include <linux/input-event-codes.h>

/* The character an evdev key code types on a US layout, or 0 when the code is
 * not a character key.
 *
 * This exists because the Hibr adapter computed it as `'a' + code - KEY_A`,
 * which assumes the letter codes run alphabetically. They do not: evdev
 * numbers keys by their position in the QWERTY rows.
 *
 *     KEY_A 30   KEY_S 31   KEY_D 32   KEY_F 33   KEY_G 34
 *     KEY_Q 16   KEY_W 17   KEY_E 18   KEY_Z 44   KEY_M 50
 *
 * So the arithmetic turned F into 'd' and S into 'b' -- which is what a user
 * reported, in those words -- and because KEY_Z is 44 the range KEY_A..KEY_Z
 * spanned the home row and its punctuation instead of the alphabet, leaving
 * every letter on the top and bottom rows producing no character at all.
 *
 * The digits were correct by luck: evdev does number those consecutively.
 *
 * It is a table and not arithmetic on purpose. There is no relationship here
 * to get right, and the last attempt at one is the bug this replaced. */
static char blit_evdev_char(unsigned long long code)
{
    switch (code) {
    case KEY_A: return 'a'; case KEY_B: return 'b'; case KEY_C: return 'c';
    case KEY_D: return 'd'; case KEY_E: return 'e'; case KEY_F: return 'f';
    case KEY_G: return 'g'; case KEY_H: return 'h'; case KEY_I: return 'i';
    case KEY_J: return 'j'; case KEY_K: return 'k'; case KEY_L: return 'l';
    case KEY_M: return 'm'; case KEY_N: return 'n'; case KEY_O: return 'o';
    case KEY_P: return 'p'; case KEY_Q: return 'q'; case KEY_R: return 'r';
    case KEY_S: return 's'; case KEY_T: return 't'; case KEY_U: return 'u';
    case KEY_V: return 'v'; case KEY_W: return 'w'; case KEY_X: return 'x';
    case KEY_Y: return 'y'; case KEY_Z: return 'z';
    case KEY_1: return '1'; case KEY_2: return '2'; case KEY_3: return '3';
    case KEY_4: return '4'; case KEY_5: return '5'; case KEY_6: return '6';
    case KEY_7: return '7'; case KEY_8: return '8'; case KEY_9: return '9';
    case KEY_0: return '0';
    /* The punctuation keys produced nothing at all before, for the same
     * reason: their codes fall outside the range the arithmetic used. */
    case KEY_MINUS: return '-'; case KEY_EQUAL: return '=';
    case KEY_LEFTBRACE: return '['; case KEY_RIGHTBRACE: return ']';
    case KEY_SEMICOLON: return ';'; case KEY_APOSTROPHE: return '\'';
    case KEY_GRAVE: return '`'; case KEY_BACKSLASH: return '\\';
    case KEY_COMMA: return ','; case KEY_DOT: return '.';
    case KEY_SLASH: return '/'; case KEY_SPACE: return ' ';
    default: return 0;
    }
}

#endif
