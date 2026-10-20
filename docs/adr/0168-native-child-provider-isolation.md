# ADR0168: native child shells must not inherit the optional API provider

- Date: 2026-10-20
- Status: implemented in candidate0.1.584; live acceptance pending
- Tracking: [Hibr321](https://git.home.arpa/itdlabs/hibr/issues/321)

The current package already keeps `console-blit.so` outside Hibr's module
directory (ADR0038). Live583 confirms native `console` is the only discovered
display provider. However, Blit's launcher also exported a startup RC that
unconditionally loads the optional adapter into interactive children. A child
terminal with no API surface then fails with `BLIT_SURFACE_ID is missing`.

Only explicitly API-provider launches receive Blit's default adapter RC.
Export the resolved `BLIT_HIBR_PROVIDER` for children; it is the same existing
provider choice, not an additional configuration store. Preserve caller
`HIBR_RC` and explicit fixture runtime RC. The adapter RC itself loads only
when `BLIT_HIBR_PROVIDER=api`. The second guard protects environments from
older launchers that still carry the RC path. Do not infer API intent from an
inherited surface ID or alter Hibr's core autoloader/provider priority.

Actual installed-Hibr tests verify default/native selection, old RC-path
inheritance, explicit API selection and caller RC preservation. The old583
packaged RC fails that regression. The current package must still contain
the adapter only in its Blit-owned path. Hibr core and installed Hibr modules
remain untouched. Existing interactive shells may already have the adapter
loaded; changing an RC file does not unload it. Reopen such shells after
rollout rather than pretending their running provider changed.
