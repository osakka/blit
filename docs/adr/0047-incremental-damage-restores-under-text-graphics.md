# ADR-0047: Restore under-text graphics on incremental damage

Status: accepted

## Context

Hibr wallpapers are represented as complete negative-z Kitty/API graphics
behind the terminal cells. The SDL renderer caches the composed scene and
uses incremental damage for cell updates and pointer movement. Clearing a
damaged cell rectangle without repainting the under-text graphics removes the
wallpaper from that rectangle until a later full scene rebuild, producing the
black holes, blinking, and visual trails reported on the live desktop.

## Decision

When an SDL output has incremental cell damage, Blit redraws the negative-z
graphics clipped to that damage rectangle before repainting terminal cells.
Full redraws use the same ordering with a full-surface clip. Hibr remains an
external producer; no Hibr source or protocol behavior is changed.

## Verification

`tools/tests/sdl_wallpaper_damage_gate.rb` creates an opaque negative-z Kitty
wallpaper, commits a separate terminal-cell damage update, and compares a
pixel in the damaged region before and after the update. The gate must report
`SDL_WALLPAPER_DAMAGE_PASS`.
