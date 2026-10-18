# ADR 0129: SDL KMSDRM hotplug crosses the dependency boundary

- Status: proposed; local SDL extension decision pending
- Date: 2026-10-18
- Issue: [Blit#100](https://git.home.arpa/itdlabs/blit/issues/100)
- Related: ADRs0124,0128; hibr-apps ADR0002

## Evidence

Blit's `discover_displays()` runs only at startup; its SDL event pump has no
display add/remove reconciliation. More importantly, the bundled SDL source
enumerates DRM connectors in `KMSDRM_InitDisplays()` and adds its internal
display objects there. `KMSDRM_PumpEvents()` polls evdev/wscons input only.
It does not monitor DRM connector changes, add new SDL display objects or
emit display-added/removed events after startup. Calling SDL_GetDisplays
again or merely implementing a Blit event handler cannot fill that gap.

The vendored SDL AGENTS/CLAUDE guidance discourages AI contributions. Before
editing the dependency, direction was requested for a version-pinned local
patch, with no upstream submission. Hibr modifications remain out of scope.
No SDL or renderer change has been made at this checkpoint.

## Proposed implementation boundary

If approved, maintain one documented dependency extension and one Blit
reconciler, not a parallel DRM renderer or connector inventory masquerading
as usable SDL outputs. SDL retains ownership of connector-to-display objects,
windows, contexts and scanout. Blit consumes its inventory/events and owns
resource reconciliation, topology and the existing auxiliary policy.

Preserve unchanged output resources, renderer/font/settings, authenticated
primary lifetime and confirmed user placements. Bound/debounce hotplug work;
detect backend failures explicitly. Added outputs are unconfigured unless a
saved/live binding selects them. Missing outputs retain intent, and auxiliary
return must not create duplicate owners or clients. Primary loss is not an
implicit authorization for primary migration.

Test each boundary separately: SDL connector changes/events and cleanup;
Blit add/remove/reorder/failure reconciliation and stable bindings; native
standby recovery; then actual second/third monitor add/remove on10.54 without
service restart. Dummy events or API inventory alone do not accept scanout.

## Alternatives

- Use an upstream released driver with verified hotplug support: acceptable
  if one is identified and tested; none has been established in this review.
- Restart SDL video or Blit on connector changes: rejected as seamless
  hotplug; it destroys working output resources and can disrupt the desktop.
- Poll DRM into a second Blit-only display list: rejected; SDL still cannot
  create a real output for a connector absent from its own inventory.
- Private SDL structure mutation or alternate rendering backend: rejected
  as an undocumented ABI dependency or parallel implementation.

## Current result

Issue100 is diagnosed, not implemented or physically accepted. The working
0.1.543 service remains unchanged. Panel0.1.2 fixes its own aggressive polling
independently; it does not add hardware hotplug.
