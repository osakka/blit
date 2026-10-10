/* ADR 0064: evdev device identity is derived from the device, not its node.
 *
 * This gate exists because the environment the gates run in has no
 * /dev/input and no /dev/uinput, so neither hotplug nor a real EVIOCGID can
 * be exercised locally. The derivation is therefore a pure function over the
 * identity block and the two device-reported strings, and this probe asserts
 * over fabricated identities -- including the exact three-node case the live
 * host presents.
 */
#include "../../src/blit_input_identity.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0)
        return;
    fprintf(stderr, "input identity gate: %s: got %s, want %s\n", what, got,
            want);
    ++failures;
}

static void expect_differs(const char *what, const char *left,
                           const char *right)
{
    if (strcmp(left, right) != 0)
        return;
    fprintf(stderr, "input identity gate: %s: both derived %s\n", what, left);
    ++failures;
}

int main(void)
{
    char remote_keyboard[BLIT_INPUT_ID_CAPACITY];
    char remote_mouse[BLIT_INPUT_ID_CAPACITY];
    char remote_consumer[BLIT_INPUT_ID_CAPACITY];
    char serialled[BLIT_INPUT_ID_CAPACITY];
    char repositioned[BLIT_INPUT_ID_CAPACITY];
    char virtual_first[BLIT_INPUT_ID_CAPACITY];
    char virtual_second[BLIT_INPUT_ID_CAPACITY];
    char bare_first[BLIT_INPUT_ID_CAPACITY];
    char bare_second[BLIT_INPUT_ID_CAPACITY];
    char slug[BLIT_INPUT_SLUG_CAPACITY];

    /* The live host's "ZYSD.Ltd HCY MIC RC": one USB device presenting three
       event nodes that share one input_id. They are distinguished only by the
       interface suffix of their phys string, so this is the case that decides
       whether phys belongs in the id at all. */
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input0",
                         "/dev/input/event4", 0, remote_keyboard,
                         sizeof(remote_keyboard));
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input1",
                         "/dev/input/event5", 0, remote_mouse,
                         sizeof(remote_mouse));
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input2",
                         "/dev/input/event6", 0, remote_consumer,
                         sizeof(remote_consumer));
    expect("remote keyboard", remote_keyboard,
           "input-0003-1d57-fa20-usb-0000-00-14.0-7-input0");
    expect_differs("remote keyboard vs mouse", remote_keyboard, remote_mouse);
    expect_differs("remote mouse vs consumer control", remote_mouse,
                   remote_consumer);
    expect_differs("remote keyboard vs consumer control", remote_keyboard,
                   remote_consumer);

    /* A device that reports a serial is identified by it, so the id survives
       being moved to a different port. A device that reports only a physical
       path does not -- that is a property of the hardware, and the contract
       says so rather than pretending otherwise. */
    blit_input_device_id(0x0003, 0x046d, 0xc52b, "A1B2C3D4",
                         "usb-0000:00:14.0-3/input0", "/dev/input/event9", 0,
                         serialled, sizeof(serialled));
    expect("serialled device", serialled, "input-0003-046d-c52b-a1b2c3d4");
    blit_input_device_id(0x0003, 0x046d, 0xc52b, "A1B2C3D4",
                         "usb-0000:00:14.0-9/input0", "/dev/input/event17", 0,
                         repositioned, sizeof(repositioned));
    expect("a serial outranks the port", repositioned, serialled);

    /* A kernel-synthesised device reports nothing. The node is all there is,
       and the id says that instead of inventing stability. */
    blit_input_device_id(0, 0, 0, "", "", "/dev/input/event0", 0, virtual_first,
                         sizeof(virtual_first));
    blit_input_device_id(0, 0, 0, NULL, NULL, "/dev/input/event1", 0,
                         virtual_second, sizeof(virtual_second));
    expect("virtual device", virtual_first, "input-node-event0");
    expect_differs("two virtual devices", virtual_first, virtual_second);

    /* Two devices that are genuinely indistinguishable -- same bus, same
       model, no serial, no phys -- still get distinct ids, by scan order. */
    blit_input_device_id(0x0003, 0x1234, 0x5678, "", "", "/dev/input/event20",
                         0, bare_first, sizeof(bare_first));
    blit_input_device_id(0x0003, 0x1234, 0x5678, "", "", "/dev/input/event21",
                         1, bare_second, sizeof(bare_second));
    expect("first indistinguishable device", bare_first, "input-0003-1234-5678");
    expect("second indistinguishable device", bare_second,
           "input-0003-1234-5678-1");

    /* The slug is a function of meaningful content, not punctuation: runs of
       rejected bytes collapse to one separator and the ends are trimmed, so
       an id never carries a dangling or doubled '-'. */
    blit_input_slug("  Foo//Bar::Baz!! ", slug, sizeof(slug));
    expect("slug collapses separators", slug, "foo-bar-baz");
    blit_input_slug("", slug, sizeof(slug));
    expect("empty slug", slug, "");

    if (failures > 0) {
        fprintf(stderr, "input identity gate: %d assertion(s) failed\n",
                failures);
        return 1;
    }
    printf("INPUT_IDENTITY_PASS remote_nodes=3 distinct=3 serial_wins=1 "
           "virtual_fallback=%s ordinal=%s\n", virtual_first, bare_second);
    return 0;
}
