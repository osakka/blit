/* Stable identity for an evdev input device (ADR 0064).
 *
 * A device's `/dev/input/eventN` node number is assigned in discovery order,
 * so it renumbers on replug and on reboot. Any configuration a control panel
 * saves against `event-5` can therefore land on a different device after a
 * restart. The id here is derived from what the device reports about itself
 * instead:
 *
 *     input-<bustype>-<vendor>-<product>[-<slug>][-<ordinal>]
 *
 * with bustype, vendor and product as four lowercase hex digits from
 * EVIOCGID, and `slug` from a sanitised EVIOCGUNIQ (the device's own serial)
 * when it has one, else EVIOCGPHYS (its topological position, for example
 * "usb-0000:00:14.0-1/input0").
 *
 * The physical path is the disambiguator that matters in practice: the live
 * host's remote exposes three event nodes -- keyboard, mouse and consumer
 * control -- that share one input_id and differ only in the interface suffix
 * of their phys string. Without it, three devices collapse to one id.
 *
 * This is exactly as stable as udev's own by-id naming and no more: moving a
 * device to a different USB port changes its phys and therefore its id,
 * unless the device reports a serial. That is a property of the hardware.
 *
 * The derivation is a pure function over the identity block and the two
 * strings so that it can be gated without hardware: the gate environment has
 * no /dev/input and no /dev/uinput. tools/tests/input_identity_gate.c
 * includes this header and asserts over fabricated identities.
 */
#ifndef BLIT_INPUT_IDENTITY_H
#define BLIT_INPUT_IDENTITY_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BLIT_INPUT_ID_CAPACITY 128
#define BLIT_INPUT_SLUG_CAPACITY 48

/* Reduce a device-reported string to the ResourceId alphabet.
 *
 * ResourceId is `^[A-Za-z0-9._:-]+$`; a colon is legal but is already the
 * separator inside a phys string ("usb-0000:00:14.0-1/input0"), so it is
 * folded too. Everything outside [a-z0-9._-] becomes a single '-', runs
 * collapse, and leading and trailing separators are dropped -- so the slug is
 * a function of the string's meaningful content, not of its punctuation. */
static size_t blit_input_slug(const char *text, char *out, size_t capacity)
{
    size_t used = 0;
    size_t index;
    int pending = 0;

    if (capacity == 0)
        return 0;
    out[0] = '\0';
    if (text == NULL)
        return 0;
    for (index = 0; text[index] != '\0'; ++index) {
        unsigned char character = (unsigned char)text[index];
        int keep;

        if (character >= 'A' && character <= 'Z')
            character = (unsigned char)(character - 'A' + 'a');
        keep = (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_';
        if (!keep) {
            if (used > 0)
                pending = 1;
            continue;
        }
        if (pending) {
            if (used + 1 >= capacity)
                break;
            out[used++] = '-';
            pending = 0;
        }
        if (used + 1 >= capacity)
            break;
        out[used++] = (char)character;
    }
    out[used] = '\0';
    return used;
}

/* The basename of a device node, with no directory and no extension logic:
   "/dev/input/event4" becomes "event4". Used only as the last resort, when a
   device reports neither an identity block nor either string. */
static size_t blit_input_node_slug(const char *node, char *out, size_t capacity)
{
    const char *last_slash;

    if (node == NULL)
        return blit_input_slug(NULL, out, capacity);
    last_slash = strrchr(node, '/');
    return blit_input_slug(last_slash != NULL ? last_slash + 1 : node, out,
                           capacity);
}

/* Derive the id. `bustype`, `vendor` and `product` come from EVIOCGID;
   `uniq` and `phys` may be NULL or empty. `ordinal` is zero for the first
   device deriving a given id and counts up for each later collision, so two
   devices that are genuinely indistinguishable still get distinct ids in scan
   order.
 *
 * EVIOCGID's `version` is deliberately *not* part of the id: a firmware
 * update changes it, and a saved configuration should survive one. It is
 * reported in the item's `identity` block instead. */
static void blit_input_device_id(unsigned bustype, unsigned vendor,
                                 unsigned product, const char *uniq,
                                 const char *phys, const char *node,
                                 int ordinal, char *out, size_t capacity)
{
    char slug[BLIT_INPUT_SLUG_CAPACITY];
    char ordinal_suffix[16];
    size_t slug_length;

    if (capacity == 0)
        return;
    slug_length = blit_input_slug(uniq, slug, sizeof(slug));
    if (slug_length == 0)
        slug_length = blit_input_slug(phys, slug, sizeof(slug));
    ordinal_suffix[0] = '\0';
    if (ordinal > 0)
        snprintf(ordinal_suffix, sizeof(ordinal_suffix), "-%d", ordinal);
    if (bustype == 0 && vendor == 0 && product == 0 && slug_length == 0) {
        /* A device with no identity at all -- a virtual or kernel-synthesised
           device. The node is all there is, and it is not stable; saying so
           in the id is better than inventing stability. */
        char node_slug[BLIT_INPUT_SLUG_CAPACITY];
        if (blit_input_node_slug(node, node_slug, sizeof(node_slug)) == 0)
            snprintf(node_slug, sizeof(node_slug), "unknown");
        snprintf(out, capacity, "input-node-%s%s", node_slug, ordinal_suffix);
        return;
    }
    if (slug_length > 0)
        snprintf(out, capacity, "input-%04x-%04x-%04x-%s%s", bustype & 0xffffu,
                 vendor & 0xffffu, product & 0xffffu, slug, ordinal_suffix);
    else
        snprintf(out, capacity, "input-%04x-%04x-%04x%s", bustype & 0xffffu,
                 vendor & 0xffffu, product & 0xffffu, ordinal_suffix);
}

#endif /* BLIT_INPUT_IDENTITY_H */
