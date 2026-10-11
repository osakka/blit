/* Issue #68. The percent/raw arithmetic and the control-resolution rule.
 *
 * A C gate because a fabricated control node cannot serve an ioctl, so the
 * parts worth pinning exactly are the pure ones -- the same division
 * blit_input_pointer.h and pointer_curve_gate.c already use. The ioctls
 * themselves are measured on 192.168.10.54 by
 * tools/probes/host_audio_controls.rb.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/blit_audio_control.h"

static int failures;

static void expect(int got, int want, const char *what)
{
    if (got != want) {
        printf("AUDIO_CONTROL_FAIL %s: got %d want %d\n", what, got, want);
        ++failures;
    }
}

int main(void)
{
    int percent;

    /* The live host's own ranges. */
    expect(blit_audio_raw_for_percent(0, 0, 87), 0, "0% of 0..87");
    expect(blit_audio_raw_for_percent(50, 0, 87), 44, "50% of 0..87");
    expect(blit_audio_raw_for_percent(100, 0, 87), 87, "100% of 0..87");
    expect(blit_audio_raw_for_percent(50, 0, 255), 128, "50% of 0..255");
    expect(blit_audio_raw_for_percent(50, 0, 9216), 4608, "50% of 0..9216");
    expect(blit_audio_percent_for_raw(44, 0, 87), 51, "44 of 0..87");
    expect(blit_audio_percent_for_raw(60, 0, 87), 69, "60 of 0..87");
    expect(blit_audio_percent_for_raw(102, 0, 255), 40, "102 of 0..255");
    expect(blit_audio_percent_for_raw(7743, 0, 9216), 84, "7743 of 0..9216");

    /* The property that matters, and it is weaker than the obvious one for a
     * reason that is arithmetic rather than a shortcoming.
     *
     * Two maps: f takes a percentage to a raw value, g takes a raw value to a
     * percentage. `g(f(p)) == p` for every percentage needs f injective, so
     * at least 101 raw values -- a span of 100 or more. `f(g(r)) == r` for
     * every raw value needs g injective, so at most 101 of them -- a span of
     * 100 or less. They are complementary: both hold only at exactly 100
     * steps, and neither holds everywhere. The first attempt here asserted
     * the first one universally and was nonsense; the second attempt
     * asserted the second and was nonsense in the other direction.
     *
     * What holds for every range is that the cycle **settles after one
     * application**: a control panel that reads a value, shows a percentage
     * and writes it back may snap once, and never moves again. That is the
     * no-drift property, and it is what rounding both ways buys. */
    {
        static const int ranges[][2] = {
            { 0, 87 }, { 0, 255 }, { 0, 9216 }, { 0, 1 }, { 0, 3 },
            { -10239, 400 }
        };
        size_t range;
        for (range = 0; range < sizeof(ranges) / sizeof(ranges[0]); ++range) {
            int minimum = ranges[range][0];
            int maximum = ranges[range][1];
            int raw;
            int previous_percent = -1;
            for (raw = minimum; raw <= maximum; ++raw) {
                int shown = blit_audio_percent_for_raw(raw, minimum, maximum);
                int written = blit_audio_raw_for_percent(shown, minimum,
                                                         maximum);
                int shown_again = blit_audio_percent_for_raw(written, minimum,
                                                              maximum);
                if (shown_again != shown) {
                    printf("AUDIO_CONTROL_FAIL %d..%d drifts at %d: shows "
                           "%d%%, writes %d, which shows %d%%\n", minimum,
                           maximum, raw, shown, written, shown_again);
                    ++failures;
                    break;
                }
                if (shown < previous_percent) {
                    printf("AUDIO_CONTROL_FAIL %d..%d is not monotonic at "
                           "%d: %d%% after %d%%\n", minimum, maximum, raw,
                           shown, previous_percent);
                    ++failures;
                    break;
                }
                previous_percent = shown;
            }
        }
        for (range = 0; range < sizeof(ranges) / sizeof(ranges[0]); ++range) {
            int minimum = ranges[range][0];
            int maximum = ranges[range][1];
            int percent_index;
            for (percent_index = 0; percent_index <= 100; ++percent_index) {
                int raw = blit_audio_raw_for_percent(percent_index, minimum,
                                                      maximum);
                int shown = blit_audio_percent_for_raw(raw, minimum, maximum);
                int again = blit_audio_raw_for_percent(shown, minimum,
                                                        maximum);
                if (again != raw) {
                    printf("AUDIO_CONTROL_FAIL %d..%d drifts from %d%%: "
                           "writes %d, shows %d%%, writes %d\n", minimum,
                           maximum, percent_index, raw, shown, again);
                    ++failures;
                    break;
                }
            }
        }
    }
    /* A range of a hundred steps or more holds every percentage exactly. */
    for (percent = 0; percent <= 100; ++percent) {
        int raw = blit_audio_raw_for_percent(percent, 0, 255);
        int back = blit_audio_percent_for_raw(raw, 0, 255);
        if (back != percent) {
            printf("AUDIO_CONTROL_FAIL 0..255 does not round-trip %d%%: "
                   "raw %d reads back %d%%\n", percent, raw, back);
            ++failures;
        }
    }
    /* A coarser one cannot, and by exactly as much as its coarseness: 88
       values cannot distinguish 101 percentages, so a percentage comes back
       at most one away. */
    for (percent = 0; percent <= 100; ++percent) {
        int raw = blit_audio_raw_for_percent(percent, 0, 87);
        int back = blit_audio_percent_for_raw(raw, 0, 87);
        int error = back - percent;
        if (error < 0) error = -error;
        if (error > 1) {
            printf("AUDIO_CONTROL_FAIL 0..87 moved %d%% to %d%%\n", percent,
                   back);
            ++failures;
        }
    }
    /* A range too coarse to hold a percentage cannot be made to, and the
       honest answer is the value the card holds. 50% of a two-value control
       is its maximum, which reads back as 100%. */
    expect(blit_audio_raw_for_percent(50, 0, 1), 1, "50% of 0..1");
    expect(blit_audio_percent_for_raw(1, 0, 1), 100, "1 of 0..1");
    expect(blit_audio_raw_for_percent(49, 0, 1), 0, "49% of 0..1");

    /* Out of range clamps rather than wrapping, and a degenerate range is the
       minimum rather than a division by zero. */
    expect(blit_audio_raw_for_percent(-10, 0, 87), 0, "a negative percentage");
    expect(blit_audio_raw_for_percent(500, 0, 87), 87, "a percentage past 100");
    expect(blit_audio_raw_for_percent(50, 5, 5), 5, "a single-value range");
    expect(blit_audio_percent_for_raw(5, 5, 5), 0, "reading one back");
    expect(blit_audio_percent_for_raw(-5, 0, 87), 0, "a raw below the floor");
    expect(blit_audio_percent_for_raw(900, 0, 87), 100, "a raw past the ceiling");
    /* A non-zero minimum, which dB-scaled cards report. */
    expect(blit_audio_raw_for_percent(0, -10239, 400), -10239,
           "0% of a negative floor");
    expect(blit_audio_raw_for_percent(100, -10239, 400), 400,
           "100% of a negative floor");
    expect(blit_audio_percent_for_raw(-4920, -10239, 400), 50,
           "the midpoint of a negative floor");

    /* The resolution rule, with the four shapes the live host actually has. */
    {
        static const char *const master_card[] = {
            "Master Playback Volume", "Master Playback Switch",
            "PCM Playback Volume", "Capture Volume"
        };
        static const char *const sole_pcm[] = {
            "PCM Playback Switch", "PCM Playback Volume",
            "Mic Capture Switch", "Mic Capture Volume"
        };
        static const char *const ambiguous[] = {
            "Mic Playback Volume", "Speaker Playback Volume",
            "Mic Capture Volume"
        };
        static const char *const capture_only[] = {
            "Mic Capture Switch", "Mic Capture Volume"
        };
        static const char *const switches_only[] = {
            "IEC958 Playback Switch", "IEC958 Playback Switch"
        };
        expect(blit_audio_resolve_playback(master_card, 4, "Volume"), 0,
               "Master wins even where PCM Playback Volume also exists");
        expect(blit_audio_resolve_playback(master_card, 4, "Switch"), 1,
               "and for the switch");
        expect(blit_audio_resolve_playback(sole_pcm, 4, "Volume"), 1,
               "the only playback volume resolves");
        expect(blit_audio_resolve_playback(sole_pcm, 4, "Switch"), 0,
               "and the only playback switch");
        expect(blit_audio_resolve_playback(ambiguous, 3, "Volume"), -1,
               "two candidates and no Master is ambiguous, not a guess");
        expect(blit_audio_resolve_playback(capture_only, 2, "Volume"), -1,
               "a capture-only card has no playback volume");
        expect(blit_audio_resolve_playback(switches_only, 2, "Volume"), -1,
               "nor has a card with only playback switches");
        expect(blit_audio_resolve_playback(switches_only, 2, "Switch"), -1,
               "and two identical switches are still two candidates");
        expect(blit_audio_resolve_playback(master_card, 0, "Volume"), -1,
               "an empty card resolves nothing");
    }

    if (failures > 0) {
        printf("AUDIO_CONTROL_FAIL %d assertion(s)\n", failures);
        return 1;
    }
    printf("AUDIO_CONTROL_PASS settles_after_one_cycle_on_six_ranges "
           "monotonic percent_exact_above_100_steps "
           "resolution=master_then_sole_then_none\n");
    return 0;
}
