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
    char shared_phys_first[BLIT_INPUT_ID_CAPACITY];
    char shared_phys_second[BLIT_INPUT_ID_CAPACITY];
    char identical_first[BLIT_INPUT_ID_CAPACITY];
    char identical_second[BLIT_INPUT_ID_CAPACITY];
    char slug[BLIT_INPUT_SLUG_CAPACITY];

    /* The live host's "ZYSD.Ltd HCY MIC RC": one USB device presenting three
       event nodes that share one input_id. They are distinguished only by the
       interface suffix of their phys string, so this is the case that decides
       whether phys belongs in the id at all. */
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input0",
                         "ZYSD.Ltd HCY MIC RC", "/dev/input/event4", 0,
                         remote_keyboard, sizeof(remote_keyboard));
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input1",
                         "ZYSD.Ltd HCY MIC RC Mouse", "/dev/input/event5", 0,
                         remote_mouse, sizeof(remote_mouse));
    blit_input_device_id(0x0003, 0x1d57, 0xfa20, "", "usb-0000:00:14.0-7/input2",
                         "ZYSD.Ltd HCY MIC RC Consumer Control",
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
                         "usb-0000:00:14.0-3/input0", "Logitech Receiver",
                         "/dev/input/event9", 0, serialled,
                         sizeof(serialled));
    expect("serialled device", serialled, "input-0003-046d-c52b-a1b2c3d4");
    blit_input_device_id(0x0003, 0x046d, 0xc52b, "A1B2C3D4",
                         "usb-0000:00:14.0-9/input0", "Logitech Receiver",
                         "/dev/input/event17", 0, repositioned,
                         sizeof(repositioned));
    expect("a serial outranks the port", repositioned, serialled);

    /* A kernel-synthesised device reports nothing. The node is all there is,
       and the id says that instead of inventing stability. */
    blit_input_device_id(0, 0, 0, "", "", "", "/dev/input/event0", 0,
                         virtual_first, sizeof(virtual_first));
    blit_input_device_id(0, 0, 0, NULL, NULL, NULL, "/dev/input/event1", 0,
                         virtual_second, sizeof(virtual_second));
    expect("virtual device", virtual_first, "input-node-event0");
    expect_differs("two virtual devices", virtual_first, virtual_second);

    /* Two devices that are genuinely indistinguishable -- same bus, same
       model, no serial, no phys -- still get distinct ids, by scan order. */
    blit_input_device_id(0x0003, 0x1234, 0x5678, "", "", "",
                         "/dev/input/event20", 0, bare_first,
                         sizeof(bare_first));
    blit_input_device_id(0x0003, 0x1234, 0x5678, "", "", "",
                         "/dev/input/event21", 1, bare_second,
                         sizeof(bare_second));
    expect("first indistinguishable device", bare_first, "input-0003-1234-5678");
    expect("second indistinguishable device", bare_second,
           "input-0003-1234-5678-1");

    /* The live host's dongle: one HID interface, five event nodes, all
       reporting the same bus id and the same phys. A numeric ordinal over
       these is scan order, so unplugging one renumbers the rest. The name
       discriminates them, and the node that is actually read as a keyboard
       keeps an id that says what it is. */
    blit_input_device_id(0x0003, 0x0c45, 0xfefe, "",
                         "usb-0000:e6:00.4-1.1.3.3/input2",
                         "2.4G Dongle System Control", "/dev/input/event15", 1,
                         shared_phys_first, sizeof(shared_phys_first));
    blit_input_device_id(0x0003, 0x0c45, 0xfefe, "",
                         "usb-0000:e6:00.4-1.1.3.3/input2",
                         "2.4G Dongle Keyboard", "/dev/input/event17", 1,
                         shared_phys_second, sizeof(shared_phys_second));
    expect("a node sharing a phys is named by its own name",
           shared_phys_second,
           "input-0003-0c45-fefe-usb-0000-e6-00.4-1.1.3.3-input2-"
           "2.4g-dongle-keyboard");
    expect_differs("two nodes sharing one phys", shared_phys_first,
                   shared_phys_second);
    if (strlen(shared_phys_second) > 128) {
        fprintf(stderr, "input identity gate: id is %zu bytes, over the "
                "ResourceId limit of 128\n", strlen(shared_phys_second));
        ++failures;
    }

    /* Two nodes that report an identical bus id, an identical phys and an
       identical name. The live host has exactly one such pair, and nothing in
       evdev distinguishes them, so the scan-order ordinal is the last resort
       rather than the first. */
    blit_input_device_id(0x0003, 0x062a, 0x4101, "",
                         "usb-0000:e6:00.4-1.1.3.1/input1",
                         "MOSART Semi. 2.4G Keyboard Mouse",
                         "/dev/input/event11", 1, identical_first,
                         sizeof(identical_first));
    blit_input_device_id(0x0003, 0x062a, 0x4101, "",
                         "usb-0000:e6:00.4-1.1.3.1/input1",
                         "MOSART Semi. 2.4G Keyboard Mouse",
                         "/dev/input/event12", 2, identical_second,
                         sizeof(identical_second));
    expect_differs("two nodes identical in every reported field",
                   identical_first, identical_second);
    if (strstr(identical_second, "mosart") == NULL ||
        identical_second[strlen(identical_second) - 1] != '1') {
        fprintf(stderr, "input identity gate: the last resort should be the "
                "name plus an ordinal, got %s\n", identical_second);
        ++failures;
    }

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
           "shared_phys_named=1 identical_fields_ordinal=1 max_id_bytes=%zu "
           "virtual_fallback=%s\n", strlen(shared_phys_second), virtual_first);
    return 0;
}
