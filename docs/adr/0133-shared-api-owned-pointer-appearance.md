# ADR 0133: shared API-owned pointer appearance

- Status: implemented locally; physical acceptance in progress
- Date: 2026-10-18
- Issues: Blit#102 (appearance), #101 (jerk/performance)

Pointer appearance is independent of terminal cursor appearance and font scale.
Keep desired appearance in the existing settings document under `pointer`, with
shape (`arrow`, `hand`, `precision`), palette (`pearl`, `obsidian`, `neon`,
`amber`), integer pixel size24–64 and shadow boolean. Missing fields use documented
defaults. Validate before any live/settings mutation; the settings revision and
durable receipt remain authoritative. No cursor-specific second settings file.

One bounded code-native vector rasterizer produces cached antialiased RGBA
stamps for both SDL and direct KMS. Rasterize only on appearance changes, not
on motion. Use explicit hotspots, include shadow in the physical footprint,
rotate the same stamp with the output, and preserve old/new damage and pointer
background restoration. Direct KMS retains the conservative legacy pointer
when no appearance stamp has been supplied by its caller, for test/embedding
compatibility; the server supplies the canonical stamp on both backends.

Expose Blit Mouse as a second native pane in the canonical Blit catalogue
bundle. It shares connection credentials, snapshot, bounded worker and refresh
schedule with Blit, not another controller. Preserve Hibr's existing Mouse
pane, which controls window behaviour rather than Blit's device/appearance
settings. Device controls stage the existing stable-ID input configurations;
appearance and device edits share the existing revision-checked settings save.
No Hibr-core changes, shell commands on the desktop host, or uploaded executable
themes.

Supersampling and a bounded cached texture are selected over new SVG/font/image
dependencies or AI-generated cursor bitmaps: exact hotspots and scalable edges
are implementation properties, and a single source must produce both paths.
Protected private token persistence remains ADR0003 in hibr-apps. A polished
pointer does not prove input-to-photon latency or solve #101; phase timings and
real workload/cadence acceptance remain separate.
