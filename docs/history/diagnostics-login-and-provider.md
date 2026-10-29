# Login stops and child provider diagnosis (2026-10-20)

## Hibr320: reproduced without Blit

Installed Hibr0.99.270/ABI16, stock login helper, root isolated controlling
PTY (not a physical output, no user authentication):

```text
mod load sys
mod load /usr/lib/hibr/console.so
source /usr/share/hibr/desktop/login/login.hibr
console open
value=$(lg_as nobody /bin/true)
printf HELPER_RETURNED
/bin/sleep 7
```

An isolated transient systemd unit and timeout own all fixture children.
Trace only that fixture with `strace -ff -e trace=ioctl,clone,clone3,wait4,kill`.
The stock helper kills its background watchdog after the reader finishes.
Trace records watchdog SIGTERM, TCGETS, TCSETS returning ERESTARTSYS, then
kernel SIGTTOU and group stop. Its shell/sleep descendants also stop. Matched
tag source `mods/console/term.c:cn_onfatal` restores terminal state on its
inherited console descriptor. `src/exec.c:ex_bg` places the watchdog in a new
background PGID and resets SIGTTOU to default without clearing those hooks.
Live583 leader142897 was stopped in ioctl(fd120,0x5402/TCSETS), with PGID142897
different from foreground142868. This corroborates, rather than substitutes
for, the isolated signal trace.

Trace evidence: `/tmp/blit-320.XqpAbk/trace.*`; actionable reproduction and
trace excerpts are also in Hibr320. Requested upstream fix: fork-aware console
ownership/cleanup; children must not restore a parent's active console. Simply
ignoring SIGTTOU could allow the unwanted terminal mutation. No Hibr patches
or signals to live stopped children were made. Upstream implementation and
verification remain pending; this is a diagnosis, not a completed fix.

## Hibr321: Blit startup RC, not current module discovery

Live583 has no adapter at `/usr/lib/hibr/console-blit.so` or its `/lib` alias.
The adapter is package-owned under `/usr/lib/blit/hibr`; `mod avail` lists
native console as the only discoverable display provider. The launcher RC
nevertheless explicitly loads the adapter into native interactive children.
With that RC, an isolated child lacking surface/session IDs selects the
adapter and `console open` reproduces the exact missing-ID error. ADR0168
records the Blit-owned fix and acceptance boundary.

## API-only report transport

`tools/probes/api_command_report.rb 'READ_ONLY_BASH_DIAGNOSTIC'` runs a bounded
diagnostic command using a temporary unbound1x1 terminal. Set `BLIT_AUTH_TOKEN`
and optionally `BLIT_BASE_URL`. This is privileged operator tooling, not a
sandbox: the caller must select read-only diagnostics. No credentials are
printed. It returns JSON stdout/stderr/exit status through sequenced small OSC
title chunks, acknowledges each chunk through input and deletes its terminal
on completion/failure. It does not open another desktop or join. The initial
144-character chunks exceeded the existing title capacity;72-character chunks
and a lossless Unicode/long-output regression prevent truncated evidence.
Do not use terminal dimensions1x1 to assess full-screen program rendering.
