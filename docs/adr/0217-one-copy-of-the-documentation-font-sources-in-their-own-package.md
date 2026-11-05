# ADR 0217: Slimming the package: one copy of the documentation, font sources in their own package

- Status: accepted
- Date: 2026-11-05
- Scope: `packaging/build-deb.sh`, `tools/tests/package_artifact_gate.sh`,
  `assets/fonts/README.md`, `docs/development-workflow.md`
- Builds on: ADR 0195 (the Arabic companion and its pinned source), ADR 0091
  (generated release identity)

## Context

The 0.3.42 package was 23.4 MB compressed and about 52 MB installed. Two
parts were waste. The documentation tree (3.8 MB) was installed twice, at
`/usr/share/doc/blit/docs` and `/usr/lib/blit/docs`; nothing reads the
second copy, and only the package gate compared the two. And the 6.2 MB
`freefont-src-20120503.tar.gz`, the preferred-form source of GNU FreeMono
(GPLv3-or-later with the font exception, from which Blit Arabic Modern is
derived), sat in every runtime install although only a person exercising the
source offer ever needs it.

## Options considered

1. Symlink the second documentation path to the first. dpkg cannot replace a
   directory with a symlink on upgrade without maintscript helpers.
2. Drop the second copy; move the source archive to a separate package.
3. Drop the source archive and rely on the upstream URL. The GPL source offer
   would then depend on a third party's server staying up.

## Decision

Option 2. `/usr/lib/blit/docs` is no longer shipped; with its files gone from
the new package dpkg removes the old directory on upgrade. The source
archive moves to `blit-font-sources` (`Architecture: all`, version
`20120503-1`, the upstream version, independent of Blit's), installed at
`/usr/share/doc/blit-font-sources/` with a README and copyright note giving
the pinned sha256. `build-deb.sh` builds it beside the main package from
`assets/fonts/sources/`, whose hash it checks against
`arabic-fonts.source.json`. The `blit` package `Suggests: hibr,
blit-font-sources` and installs `/usr/lib/blit/fonts/FreeMono-SOURCE.txt`,
which names the package, the upstream URL and the sha256, so the source offer
travels with the binary. The repository copy and the font gates that read it
are unchanged.

## Consequences

- The main package falls from 24.6 MB to 17.2 MB (installed file bytes
  51.2 MB to 40.8 MB). The sources package is 6.5 MB and only needs
  publishing when its content changes, which is when FreeFont or the
  derivation changes; most releases publish just `blit`.
- Behaviour is unchanged: nothing at runtime reads either removed path.
- A host that wants the source installs `blit-font-sources`; `apt` does not
  pull it in (a Suggests).

## Evidence

`tools/tests/package_artifact_gate.sh` fails when `/usr/lib/blit/docs` or
`fonts/sources` is present, when `FreeMono-SOURCE.txt` or the Suggests is
missing, and, when `blit-font-sources_20120503-1_all.deb` sits beside the
package, checks its architecture, contents and that the archive's sha256 is the
manifest's `source_sha256`. An upgrade from 0.3.42 into a scratch root removes
`/usr/lib/blit/docs` and `fonts/sources` and changes nothing else apart from
the new files.
