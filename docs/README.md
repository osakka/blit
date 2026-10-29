# Blit documentation

Blit is an API-driven display server for text: it drives monitors directly
through Linux DRM/KMS, composes frames on the GPU (OpenGL ES), or on the CPU when chosen or when
there is no usable GPU,, and presents terminal sessions with images, emoji, icons and
effects. Hibr's desktop runs on it; so can any other program.

Public repository: [github.com/osakka/blit](https://github.com/osakka/blit).
Issue numbers in these documents (#111) are the internal tracker's, not
GitHub's.

## Start here

| You are… | Read, in order |
| --- | --- |
| **Trying Blit out** | the [README](../README.md), [features](features.md), `man blit` ([source](../man/blit.8)) |
| **Running it** | `man blit`, `man blit.env` ([source](../man/blit.env.5)), the [operator runbook](api.md#operator-runbook-prove-the-server-before-touching-a-desktop), [diagnostic capture](diagnostic-capture.md) |
| **Writing a client** | the [API guide](api.md), the [OpenAPI document](../openapi/blit-0.1.yaml), [API principles](spec/api-principles.md) |
| **Writing a terminal program** | the [terminal reference](terminal.md), the [font and glyph reference](font-reference.md) |
| **Integrating a desktop** | [Hibr integration](integration-hibr.md), then the Hibr ADRs in the [index](adr/README.md) |
| **Contributing** | the [engineering constitution](engineering-constitution.md), [development workflow](development-workflow.md), [repository hygiene](repository-hygiene.md), [architecture](architecture.md) |

## Everything

**Using Blit**

- [Features](features.md) — everything Blit does, when it arrived, how far
  it has been proved.
- [Terminal reference](terminal.md) — escape sequences, mouse and keyboard
  protocols, Kitty graphics, Sixel, Unicode, limits and known gaps.
- [Font and glyph reference](font-reference.md) — fonts, the glyph stack,
  Nerd Fonts, emoji and Blit's own icons, with codepoints.
- [Font assets](../assets/fonts/README.md) and [icons](../assets/fonts/icons/README.md)
  — what is shipped, its provenance and licences.
- Manual pages: [`blit(8)`](../man/blit.8), [`blit.env(5)`](../man/blit.env.5),
  [`blit-update-helper(8)`](../man/blit-update-helper.8).

**The API**

- [API guide](api.md) — discovery, sessions, displays and panes, fonts,
  input, sound, settings, diagnostics, control, the event stream, errors.
- [OpenAPI document](../openapi/blit-0.1.yaml) — the exact wire contract.
- [API principles](spec/api-principles.md) — how the API is designed.
- [Hibr integration](integration-hibr.md) — login, desktop ownership,
  joined displays, the optional console adapter.

**Design**

- [Vision and scope](vision.md) — what Blit is for, and what it is not.
- [Architecture](architecture.md) — how the server is put together.
- [Decision records](adr/README.md) — why it is the way it is.
- [Rectangle copy](rectangle-copy.md) — the DECCRA contract with Hibr.

**Building, testing, releasing**

- [Development workflow](development-workflow.md) — the cycle from change
  to release, and the required gates.
- [Engineering constitution](engineering-constitution.md) — rules every
  change follows.
- [Repository hygiene](repository-hygiene.md) — what belongs in the
  repository, staging and handoff.
- [Toolchain](toolchain.md) — TCC and the dependencies.
- [Implementation and verification contract](implementation-and-verification.md)
  — requirements, test matrix and release gates.
- Testing guides: [KMS](testing-kms.md), [bare metal](testing-baremetal.md),
  [the API gateway](testing-gateway.md), [effects](testing-effects.md).
- [Diagnostic capture](diagnostic-capture.md) — collecting evidence from a
  live machine.

**Status**

- [Roadmap](roadmap.md) — what is next, in order.
- [Verification ledger](verification-status.md) — the evidence for each
  release.
- [History](history/README.md) — earlier plans, investigations and restart notes,
  kept as records.

## Which document wins

When two disagree:

1. the [OpenAPI document](../openapi/blit-0.1.yaml), for anything on the wire;
2. the C source, for behaviour inside that contract;
3. an accepted [decision record](adr/README.md);
4. the [verification ledger](verification-status.md), for what has been proved;
5. guides and READMEs.

A guide that disagrees with the code is a bug in the guide. The
documentation gate (`tools/tests/documentation_gate.rb`) checks that every
environment variable the server reads is in `blit(8)`, that the manual
pages format cleanly, and that no link in the repository is broken.

## How far something is proved

The ledger keeps these apart, and so should anyone reporting a result:

- **specified** — in the OpenAPI document or an accepted ADR;
- **implemented** — in the source, with a focused test;
- **validated** — the package passed the relevant suites;
- **host accepted** — the exact package works on the named machine, with
  its monitors, input devices and display modes.

A passing test against the mock display does not prove physical input,
monitor count or high refresh rates on real hardware.

## Keeping the documentation right

A change to the API updates the OpenAPI document, the API guide, the
relevant ADR and a gate in the same commit. A new feature gets a row in
[features](features.md); a new environment variable gets an entry in
[`blit(8)`](../man/blit.8) (the gate enforces it). Every release adds a
ledger entry naming its package checksum and suite results.
