# ADR 0095: Execute the installed Hibr session at its own path

## Context

Hibr's installed `desktop/session.hibr` derives the location of its sibling
`desktop.hibr` from the script entrypoint (`$0`). Blit previously sourced that
file from `blit-session.hibr`. The source body therefore inherited Blit's
`$0`, and Hibr looked for `/usr/share/blit/hibr/desktop.hibr` instead of the
installed Hibr window-manager tree. The script exited cleanly after reporting
missing desktop functions, which made the login manager correctly return to
its login screen with exit status 0.

## Decision

For the native terminal-provider path, Blit's launcher executes the installed
Hibr session with:

```text
exec hibr /usr/share/hibr/desktop/session.hibr ...
```

The installed Hibr tree remains the sole source of desktop code. The
Blit-owned API-provider path continues to source the selected script in the
same Hibr process because that path explicitly owns the provider module.

## Consequences

- Hibr's sibling-relative imports resolve from Hibr's installation exactly as
  they do for a normal Hibr invocation.
- No symlink, copied desktop tree, or Hibr modification is needed.
- The terminal-provider child reloads Hibr's native modules in its normal
  process; Blit receives the resulting PTY and remains the display owner.
- The package gate must assert the native exec boundary so a future wrapper
  change cannot silently reintroduce the path bug.

## Verification

- The package artifact gate checks for the installed-session exec boundary.
- A live API-created `osakka` desktop must remain running and render the
  installed Hibr desktop before the login-manager path is accepted.
