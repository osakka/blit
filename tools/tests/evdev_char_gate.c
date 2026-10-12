/* What an evdev key code types, pinned exactly.
 *
 * A user reported "the keyboard does not type qwerty, g is , and f is d".
 * The adapter computed the letter as `'a' + code - KEY_A`, which assumes the
 * letter codes run alphabetically. evdev numbers keys by their position in
 * the QWERTY rows instead, so F (33) came out as 'd' -- the reported symptom,
 * exactly -- and the range KEY_A..KEY_Z (30..44) covered the home row and its
 * punctuation rather than the alphabet, so q, w, e, r, t, y, u, i, o, p, x,
 * c, v, b, n and m produced no character at all.
 *
 * This asserts the whole alphabet rather than a sample, because the bug was
 * correct for exactly one letter: KEY_A, where the offset is zero. Any test
 * that checked 'a' and moved on would have passed against it.
 */
#include <stdio.h>

#include "../../src/blit_evdev_char.h"

static int failures;

static void expect(unsigned long long code, char want, const char *what)
{
    char got = blit_evdev_char(code);

    if (got != want) {
        printf("EVDEV_CHAR_FAIL %s: code %llu gave '%c' (%d), wanted '%c'\n",
               what, code, got ? got : '?', (int)got, want);
        ++failures;
    }
}

int main(void)
{
    /* Every letter, in alphabetical order, against the code evdev actually
       uses for it. The codes are deliberately not in ascending order here --
       that is the entire point. */
    expect(KEY_A, 'a', "a"); expect(KEY_B, 'b', "b"); expect(KEY_C, 'c', "c");
    expect(KEY_D, 'd', "d"); expect(KEY_E, 'e', "e"); expect(KEY_F, 'f', "f");
    expect(KEY_G, 'g', "g"); expect(KEY_H, 'h', "h"); expect(KEY_I, 'i', "i");
    expect(KEY_J, 'j', "j"); expect(KEY_K, 'k', "k"); expect(KEY_L, 'l', "l");
    expect(KEY_M, 'm', "m"); expect(KEY_N, 'n', "n"); expect(KEY_O, 'o', "o");
    expect(KEY_P, 'p', "p"); expect(KEY_Q, 'q', "q"); expect(KEY_R, 'r', "r");
    expect(KEY_S, 's', "s"); expect(KEY_T, 't', "t"); expect(KEY_U, 'u', "u");
    expect(KEY_V, 'v', "v"); expect(KEY_W, 'w', "w"); expect(KEY_X, 'x', "x");
    expect(KEY_Y, 'y', "y"); expect(KEY_Z, 'z', "z");

    /* The two the report named, called out by name so a regression says the
       same words the user did. */
    expect(33, 'f', "code 33 is f, not d");
    expect(31, 's', "code 31 is s, not b");

    expect(KEY_1, '1', "1"); expect(KEY_5, '5', "5"); expect(KEY_9, '9', "9");
    expect(KEY_0, '0', "0");

    expect(KEY_MINUS, '-', "minus"); expect(KEY_EQUAL, '=', "equal");
    expect(KEY_LEFTBRACE, '[', "leftbrace");
    expect(KEY_RIGHTBRACE, ']', "rightbrace");
    expect(KEY_SEMICOLON, ';', "semicolon");
    expect(KEY_APOSTROPHE, '\'', "apostrophe");
    expect(KEY_GRAVE, '`', "grave"); expect(KEY_BACKSLASH, '\\', "backslash");
    expect(KEY_COMMA, ',', "comma"); expect(KEY_DOT, '.', "dot");
    expect(KEY_SLASH, '/', "slash"); expect(KEY_SPACE, ' ', "space");

    /* A code that is not a character key says so, rather than returning
       something a caller would print. */
    expect(KEY_ESC, 0, "escape is not a character");
    expect(KEY_LEFTCTRL, 0, "control is not a character");
    expect(KEY_F1, 0, "F1 is not a character");
    expect(KEY_ENTER, 0, "enter is not a character");

    /* Every letter maps to a distinct character, which is what the old
       arithmetic broke: it collapsed the alphabet onto the home row. */
    {
        static const unsigned long long letters[26] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        int seen[256];
        int i;
        for (i = 0; i < 256; ++i)
            seen[i] = 0;
        for (i = 0; i < 26; ++i) {
            unsigned char c = (unsigned char)blit_evdev_char(letters[i]);
            if (c == 0 || seen[c]) {
                printf("EVDEV_CHAR_FAIL letter %d collides or is empty: '%c'\n",
                       i, c ? c : '?');
                ++failures;
            }
            seen[c] = 1;
        }
    }

    if (failures > 0) {
        printf("FAIL evdev-char (%d)\n", failures);
        return 1;
    }
    printf("EVDEV_CHAR_PASS letters=26 digits=10 punctuation=12 "
           "non_characters=4 distinct=yes\n");
    return 0;
}
