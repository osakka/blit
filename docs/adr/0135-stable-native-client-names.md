# ADR 0135: stable native desktop client names

- Status: implemented/API-deployed550; native names verified, wallpaper restart acceptance pending
- Date: 2026-10-18
- Issue: Blit#103 (Hibr#284)

Hibr stores display preferences under its native client name. Boot-time and
session-counter names therefore discard that association on every restart.
Use the existing output selector (connector, otherwise SDL display name) and
the pane's one-based row-major ordinal: `blit-DP-1-p2`. Primary/auxiliary role,
runtime IDs, timestamps, font and geometry are not identity inputs.

Names are at most 63 ASCII characters from `A-Za-z0-9._:-`. Unsafe or long
selectors receive a readable sanitized prefix and a deterministic 64-bit FNV-1a
suffix over the complete selector. This hash is not a security identity;
inventory collisions and live managed-name reuse fail closed. No EDID probe,
new settings store or API resource is introduced.

One helper supplies both launch paths. Owner UID, PID/start-time and native
geometry checks remain mandatory. Refuse an occupied auxiliary native name;
detect both repeated exact names (observed in installed Hibr0.99.250) and
`name#N` collision suffixes rather than associating an unrelated base client.
No automatic fallback to another name and no second controller.

The name belongs to the pane at client creation. Moving a live client changes
geometry, not its name; the next launch derives its new target's identity.
Changing a connector/ordinal intentionally selects a different preference key.
Existing timestamp names migrate once; no Hibr preference files are rewritten.
Unchanged duplicate SDL names cannot provide distinct stable identities and
are refused, rather than distinguished with unstable numeric display IDs.

Verify pure naming and runtime collision guards with TCC, both primary and
auxiliary launch paths, the actual released Hibr native CLI, and repeat launch
without suffix accumulation. Physical wallpaper persistence remains a distinct
acceptance check requiring an authenticated desktop; tests do not imply it.
