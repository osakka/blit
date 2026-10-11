#ifndef BLIT_AUDIO_CONTROL_H
#define BLIT_AUDIO_CONTROL_H

#include <string.h>

/* An ALSA mixer element's value as a percentage, and the rule for which
   element a plain "volume" means (issue #68).
 *
 * ALSA ranges are whatever the hardware says: the live host's six cards report
 * 0..255, 0..87, 0..63, 0..36, 0..32 and 0..9216 for various elements. A
 * control panel offers a percentage, so the conversion has to happen
 * somewhere, and it is lossy -- which is why what Blit reports is the value
 * read back from the card and not the percentage that was asked for.
 *
 * Both directions round rather than truncate, and the property that buys is
 * worth stating precisely because the obvious stronger one is impossible.
 *
 * A percentage cannot survive a range with fewer than 100 steps: the host's
 * Master is 0..87, which is 88 values against 101 percentages, so by
 * pigeonhole at least thirteen percentages must collide. Asserting that every
 * percentage round-trips was the first attempt here and it was arithmetic
 * nonsense -- 50% of 0..87 is 44, and 44 of 0..87 is 51%.
 *
 * The converse is impossible too, in the other direction: raw -> percent ->
 * raw cannot be exact on a range *finer* than 100 steps, because 256 values
 * cannot be told apart by 101 percentages.
 *
 * What holds for every range is that the cycle **settles after one
 * application**. A control panel reads a value, shows a percentage, and
 * writes that percentage back when the person adjusts something else; it may
 * snap once, and it never moves again. That is the no-drift property, and it
 * is what rounding both ways buys -- truncating breaks it, because 44 would
 * show as 50% and 50% would write 43 and 43 would show as 49%.
 *
 * On a range of 100 steps or more the percentage round-trips exactly as well.
 * On a coarser one it does not, and the honest answer is the value the card
 * holds: 50% of a two-value control is its maximum, which reads back as
 * 100%.
 *
 * Here as a pure function, with the same reasoning as blit_input_pointer.h:
 * the arithmetic and the resolution rule are the parts worth pinning exactly,
 * and a gate cannot serve an ioctl from a fabricated control node.
 */

#define BLIT_AUDIO_CONTROL_NAME_CAPACITY 48

static int blit_audio_raw_for_percent(int percent, int minimum, int maximum)
{
    int span = maximum - minimum;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (span <= 0)
        return minimum;
    return minimum + (span * percent + 50) / 100;
}

static int blit_audio_percent_for_raw(int raw, int minimum, int maximum)
{
    int span = maximum - minimum;

    if (span <= 0)
        return 0;
    if (raw < minimum) raw = minimum;
    if (raw > maximum) raw = maximum;
    return ((raw - minimum) * 100 + span / 2) / span;
}

/* Which element a plain `volume` or `mute` means, stated as a rule rather
   than left to a heuristic -- because a heuristic is untestable and a control
   panel that silently adjusts the wrong element is worse than one that has to
   name the element itself.
 *
 *   1. the element named exactly "Master Playback <kind>", if there is one
 *   2. otherwise the only "* Playback <kind>" element, if there is exactly one
 *   3. otherwise nothing: two candidates and no Master is genuinely ambiguous
 *
 * The live host exercises every branch: one card has Master, one has only
 * "PCM Playback Volume", one has both "Mic Playback Volume" and "Speaker
 * Playback Volume" and no Master, and three have no playback volume at all.
 */
static int blit_audio_resolve_playback(const char *const *names, int count,
                                       const char *kind)
{
    char master[BLIT_AUDIO_CONTROL_NAME_CAPACITY];
    char suffix[BLIT_AUDIO_CONTROL_NAME_CAPACITY];
    int candidate = -1;
    int candidates = 0;
    int i;

    snprintf(master, sizeof(master), "Master Playback %s", kind);
    snprintf(suffix, sizeof(suffix), " Playback %s", kind);
    for (i = 0; i < count; ++i)
        if (strcmp(names[i], master) == 0)
            return i;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(names[i]);
        size_t tail = strlen(suffix);
        if (length <= tail)
            continue;
        if (strcmp(names[i] + length - tail, suffix) != 0)
            continue;
        candidate = i;
        ++candidates;
    }
    return candidates == 1 ? candidate : -1;
}

#endif
