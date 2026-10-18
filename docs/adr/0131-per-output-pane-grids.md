# ADR 0131: independent output pane grids

- Status: implemented in0.1.545; physical acceptance pending
- Date: 2026-10-18
- Issues: Blit#99, #100

The settings resource owns `display_layouts`, keyed by the stable connector
selector (display name when a backend supplies no connector). Each value is
`{columns, rows}`, each integer 1 through 8. There are at most 16 configured
outputs and 64 live panes. Unknown outputs default to 1x1, not another
monitor's grid. Absent outputs retain their settings.

At startup, a legacy global grid is translated to explicit entries for the
outputs present at that startup. Existing installations retain their layout;
subsequent hotplug does not inherit it. The old global API remains a legacy
bulk operation, never a second per-output configuration store.

One resolver supplies discovery, topology reporting and auxiliary lookup.
Partitions remain pixel-first, independently centred complete-cell grids.
Settings replacement validates the entire proposed inventory before mutation.
Removing an occupied pane is a 409 conflict, not implicit termination.
Surviving pane IDs retain their terminal and receive the normal PTY resize.
Configured grids exceeding physical complete-cell capacity or the live pane
limit fail explicitly. New hotplug capacity exhaustion must be diagnosed.

The catalogue pane uses Hibr's existing slider callbacks, not custom widgets.
Rows and columns are independently staged; Save applies the durable layout.
Native session counts remain bounded separately from the number of panes.

Verification: `display_layout_gate.rb` covers the matrix, persistence,
same-process resize, occupied-pane conflict and legacy bulk/offline capacity.
`kms_hotplug_gate.rb` checks that newly attached outputs default to1x1.
