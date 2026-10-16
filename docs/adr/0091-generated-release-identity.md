# ADR 0091: Generate release identity from the package

## Status

Accepted for the release tooling slice of issue #27.

## Context

The Debian package is the artifact that is installed and deployed. Its
filename, package checksum, embedded server/helper/adapter hashes, and pinned
Hibr version are therefore facts of the package, not facts an operator should
copy into a YAML file. Hand-copying those fields has already produced stale
release records in earlier releases.

The release record also contains acceptance claims and narrative notes. Those
are not derivable from an archive: a 60 Hz measurement, a physical display
count, and a Hibr desktop observation require a named test and an evidence
record. They must remain human-owned evidence rather than being fabricated by
the generator.

## Decision

`tools/release/generate-manifest.sh` owns the machine identity section. It
extracts the package into a temporary directory and derives:

- release and package name/version;
- package SHA-256;
- source and documentation commit identifiers supplied by the release
  process;
- hashes of the shipped server, update helper, and Hibr adapter;
- the packaged `HIBR_VERSION`;

The adapter build method remains an explicit evidence field because the
archive can prove the shipped adapter hash, but cannot prove which external
checkout or compiler produced it.

`tools/tests/package_artifact_gate.sh` and
`tools/tests/release_manifest_gate.sh` fail if any generated field is missing,
duplicated, or differs from the package. Existing human-owned acceptance
fields remain in `releases/<version>.yaml` and are not rewritten by the tool.

The release sequence is therefore:

1. commit source, tests, and documentation;
2. build the package from that commit;
3. generate the machine identity block with that source commit;
4. add measured acceptance fields and notes;
5. run the package and manifest gates;
6. commit and push the manifest as the evidence commit.

## Consequences

Package provenance is checked from the artifact that will be installed, and
the release gate no longer trusts manually copied hashes. The manifest still
requires explicit human evidence for physical, GPU, Hibr, and refresh claims.
This closes the provenance portion of issue #27 but does not claim CI or
automatic live-update rollback/restore.

## Rejected alternatives

- **Generate the entire manifest from package contents:** rejected because
  package contents cannot prove physical hardware, Hibr behaviour, or timing
  acceptance.
- **Keep copying fields by hand:** rejected because it has no protection
  against a stale hash or stale package identity.
- **Put acceptance notes in a second implementation/configuration format:**
  rejected; the YAML release record remains the one evidence index, with the
  generator owning only the facts it can prove.
