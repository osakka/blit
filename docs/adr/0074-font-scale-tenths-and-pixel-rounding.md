# ADR-0074: Support tenth-step font scales with deterministic pixel rounding

- Status: accepted

## Context

ADR 0047 introduced API-selectable font profiles and bounded integer scales
from 1 through 3. The live renderer now has one physical cell-metric boundary
(ADR 0073), so restricting the public value to integers is no longer useful:
operators need to tune readability between the large jumps while preserving
the same PTY, pointer, pane, SDL, and KMS geometry.

Framebuffer dimensions and PTY pixel sizes are integers. Passing a binary
floating-point scale through those boundaries would make rounding differ
between subsystems and would eventually produce input/render drift.

## Decision

`PUT /v1/fonts` accepts `scale` as a JSON number from `1.0` through `3.0` in
0.1 increments. Existing integer requests remain compatible (`2` means
`2.0`). The server stores the value as integer tenths, from 10 through 30.

Every physical cell dimension is calculated by the single rule
`round(base_pixels * scale_tenths / 10)`, implemented with integer arithmetic
as `(base_pixels * scale_tenths + 5) / 10`. Thus an 8x16 font at 1.1 is 9x18,
and the same metrics are used for SDL, direct KMS, pane discovery, PTY pixel
sizes, display-session reflow, surface origins, and pointer hit-testing.

The API reports the logical value with one decimal place and reports the
resulting whole-pixel `cell_pixels` dimensions. Values outside the range or
not representable in tenths are rejected with `invalid_font_scale`.

Hibr source and font assets are not modified.

## Verification

`tools/tests/font_api_gate.rb` must exercise every value 1.0, 1.1, …, 3.0,
verify the returned physical dimensions and display-session reflow, verify
representative rendered pixels, and reject values such as 0.9, 1.05, and
3.1. The OpenAPI, package, renderer, and complete local gate suites must
remain green. A live API check must verify the deployed version and at least
the 1.0, 1.1, 2.0, 2.3, and 3.0 physical cell sizes.

## Consequences

Font sizing is smooth enough for operator tuning without introducing a second
geometry implementation. Some adjacent tenth-step values can produce the
same physical dimension for a small base font; that is an unavoidable result
of whole-pixel hardware, and the reported `cell_pixels` value makes it
explicit. Scale changes still reflow display-attached Hibr sessions without a
restart.
