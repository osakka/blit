# Security review, November 2026 (Gitea #112)

This is the security part of release acceptance (#112). It reviews the
source at 0.3.129 (commit 20ac2081). Historical fixes (#25, #26) are not
counted as evidence. Every finding below cites the code it comes from. None
of them is fixed here. Each one ends with a ticket that can be filed as
written: a title, then a body giving the behaviour and the gate that shows
the fix.

The rollback and recovery rehearsal, the other half of #112, is at the end
under [Rollback and recovery, as rehearsed](#rollback-and-recovery-as-rehearsed).

## Context

- Blit runs as **root** by default (`packaging/blit.service`, `User=root`).
  The unprivileged profile (ADR 0065,
  `/usr/share/doc/blit/examples/blit-unprivileged.conf`) is opt-in.
- The package's `/etc/blit/blit.env` binds **0.0.0.0** port 8080 over plain
  HTTP (`packaging/build-deb.sh:320`), and generates `BLIT_AUTH_TOKEN` and
  `/etc/blit/update.token`.
- **The session API runs any command, as root, for anyone who holds the API
  token.** This is by design (ADR 0065). So the API token is root on the
  host, and so is anything that can get it or get around it.
- **This is the owner's internal test environment.** Listening on the LAN
  is a temporary, approved LAN-testing exception. Release acceptance asks for
  such exceptions to be stated, not changed silently. SR-9 states it. Nothing
  here asks for credentials to be rotated.

Method: the code was read, and line numbers were checked against this
tree. Nothing was fuzzed. The packaging and update findings were also
exercised with real dpkg in the Debian 13 release environment
(`tools/tests/rollback_recovery_gate.sh`).

## Findings

| ID | Severity | Finding |
| --- | --- | --- |
| SR-1 | Critical | A frame-channel edit writes outside the image (heap overflow in the root server) -- fixed in 0.3.130 |
| SR-2 | High | An update needs only the update token, sent in clear, and nothing checks who built the package |
| SR-3 | High | Membership of group `blit` is root: it reaches the helper socket, the staging directory and the API token |
| SR-4 | High | XTGETTCAP echoes the request back into the terminal's input (command injection by `cat`) -- fixed in 0.3.130 |
| SR-5 | High | CSI HPR/VPR/VPA can make the cursor negative, and the next character is written outside the cell arrays -- fixed in 0.3.130 |
| SR-6 | High | `session_json` can overflow the 8 KB stack buffer of `GET /v1/login` -- fixed (release and commit to be filled) |
| SR-7 | High | PTY master descriptors leak into every later session child, including ones run as another user -- fixed in 0.3.130 |
| SR-8 | High | The root API token is put into the environment of children run as another user |
| SR-9 | High, accepted exception | The API token is a root shell, sent over plain HTTP on 0.0.0.0, with no default command allowlist |
| SR-10 | Medium | The helper reads the staged package three times by path, in a group-writable directory |
| SR-11 | Medium | Both tokens are compared with `strcmp` -- fixed (release and commit to be filled) |
| SR-12 | Medium | An install blocks the server's main loop with no timeout; the console watchdog kills it at 120 s, mid-install |
| SR-13 | Medium | dpkg accepts `blit` N-1 alone under `blit-compositor` N; only the helper keeps the pair together |
| SR-14 | Medium | blit-compositor can be killed with SIGBUS through a memfd passed as a dmabuf |
| SR-15 | Medium | A viewport source rectangle or buffer scale makes the drag-icon scaler read outside the buffer |
| SR-16 | Medium | One CHT sequence (`CSI 2147483647 I`) stalls the server -- fixed (release and commit to be filled) |
| SR-17 | Medium | Connection slots can be used up before authentication (HTTP and the frame socket) |
| SR-18 | Low | `png_decode` leaks its compressed buffer on some error paths -- fixed (release and commit to be filled) |
| SR-19 | Low | HTTP headers are found by `strstr` over the whole request, case-sensitively -- fixed (release and commit to be filled) |
| SR-20 | Low | The JSON key scanner matches keys at any depth |
| SR-21 | Low | A failed install reports the head of dpkg's output, not the error |
| SR-22 | Low | With no token (loopback only), nothing checks `Origin` or `Host` -- fixed (release and commit to be filled) |
| SR-23 | Info | OSC 52 text is stored unchecked; the version is sent before authentication |

### SR-1 (Critical): a frame-channel edit writes outside the image

**Fixed** in 0.3.130 (52825e37): an edit must name its image's own size,
or it is refused `EINVAL` (`frame-size`); frame_descriptor_gate checks it.

A frame edit (not `whole`) checks its rectangle against the width and height
in the **message**, but writes at an offset computed from the **stored
image's** width. Nothing checks that the two agree.

- `src/blit_frames.h:564-565`, the rectangle is checked against `f->width`
  and `f->height`.
- `src/blit_frames.h:625-627`: `image_stride = (size_t)image->width * bpp; at = image->pixels + (size_t)f->y * image_stride + (size_t)f->x * bpp`,
  then `frame_read_rows` reads the client's memfd into it.
- `frame_image_for` (`src/blit_frames.h:666-700`) matches the id, the
  descriptor flag and the sequence, nothing else. The size check at 594-595
  applies only to a whole frame.

Who can do it: any process with the session's uid and its
`BLIT_FRAME_TOKEN`. Every program in a desktop session has the token in its
environment. The sequence is a 1×1 whole frame, then an edit whose header
says 4096×4096 at x=y=4000. The root server then writes bytes the attacker
chose to an offset the attacker chose: local privilege escalation.

Fix: for an edit, check the rectangle against `image->width` and
`image->height`, or refuse an edit whose header size differs from the
image's.

> **Ticket: Frame edit is bounded by the message's size, not the image's (heap overflow)**
>
> A frame edit on the frame channel is checked against the width and height
> in its own header (`blit_frames.h:564`), but it writes using the stored
> image's stride (`blit_frames.h:625`). An edit that claims a larger image
> than the one stored writes past the end of `image->pixels` in the root
> server. Wanted: an edit whose header width/height differ from the image's,
> or whose rectangle leaves the image, is refused with `frame-bounds`, and
> nothing is written. Gate: a frame-channel gate sends a 1×1 whole frame,
> then an edit claiming 4096×4096 at 4000,4000, expects the refusal, and
> runs under ASan with no report. The same gate also covers an edit inside
> the image that still applies.

### SR-2 (High): an update needs only the update token, sent in clear, and nothing checks who built the package

- `PUT /v1/control/update` is handled before `handle_request`
  (`src/blit_server.c:32636-32650`), so the bearer token is never checked
  on it. The one check is `update_authorized`
  (`src/blit_server.c:32070-32076`), a `strcmp` of `X-Blit-Update-Token`.
- The SHA-256 comes from the same request (`X-Blit-Packages`), so it only
  shows that the bytes arrived intact. The helper checks the file against
  that same caller-supplied hash (`src/blit_update_helper.c:218`), checks
  the `Package` and `Version` fields (222-225), and runs `dpkg --install`
  (261). There is no signature, and no check against the release manifest.
- The manifest is checked only on the operator's side, by
  `tools/deploy/update-live.sh` (lines 47-63), which an attacker does not
  run.
- Everything goes over plain HTTP (SR-9). One deployment seen on the LAN is
  enough to replay the update token later with any `.deb` named `blit`.
  Its maintainer scripts run as root.

Fix: the helper verifies a detached signature against a key the package
pins (or the hibr-apt archive key), and refuses anything unsigned. Also
require the bearer token on the update route. TLS (SR-9) would keep the
token off the wire.

> **Ticket: The update helper installs any .deb whose SHA-256 the caller supplies**
>
> `blit-update-helper` installs any `blit`/`blit-compositor` package whose
> SHA-256 matches the one in the request, and the request comes from the
> same client that sent the package. Holding the update token (sent in clear
> on every deploy) is therefore enough to run maintainer scripts as root.
> Wanted: the helper installs only packages carrying a valid signature from
> the release key (pinned in the installed package), and the update route
> also requires the API bearer token. Gate: in the release environment,
> `update_pair_gate` gains cases where an unsigned package and a package
> signed with another key are both refused with nothing installed, and the
> signed release pair installs. An HTTP gate shows that `PUT
> /v1/control/update` without `Authorization` gets a 401.

### SR-3 (High): group `blit` is root

- The helper's socket is root:blit 0660 (`src/blit_update_helper.c:297`,
  `packaging/blit-update-helper.service:8-9`). It accepts any peer: there is
  no `SO_PEERCRED` check at `accept` (300).
- The staging directory is root:blit 0770 (`packaging/build-deb.sh:382`).
  Any group member can put a `blit-update-*` file there, send the helper
  its hash, and have it installed as root (SR-2).
- `/etc/blit/blit.env` is root:blit 0640 and holds `BLIT_AUTH_TOKEN`
  (`packaging/build-deb.sh:340`, `354-358`). The token is a root shell
  (SR-9).
- The postinst adds the installing user to `blit`
  (`packaging/build-deb.sh:311`: `usermod -a -G blit "$runtime_user"`).
- Under the unprivileged profile, the server, and with it every session it
  starts, runs as user `blit` (`packaging/blit-unprivileged.conf:33`). So
  every terminal program can reach the root helper. The profile cannot
  contain anything.

Fix: the helper verifies signatures (SR-2), so access to the socket no
longer means choosing the package. The helper also checks `SO_PEERCRED`
against the service's uid. Sessions run under a uid other than the
server's. The staging directory belongs to the server's user, 0700. The
postinst stops adding the installer to `blit`, or the API token moves to a
file that only the service can read.

> **Ticket: Membership of group blit gives root (helper socket, staging directory, API token)**
>
> The update helper's socket and the staging directory are open to group
> `blit`. The helper installs any package a peer stages and hashes, and
> `blit.env` (group-readable) holds the root-equivalent API token. The
> postinst adds the installing user to the group, and under the
> unprivileged profile every session runs as `blit`. Wanted: a member of
> `blit` that is not the server process cannot get a package installed or
> read the API token. Gate: in the release environment, a process running as
> a non-root user in group `blit` stages a package and asks the helper. It
> is refused and nothing is installed. The same user cannot read the API
> token.

### SR-4 (High): XTGETTCAP echoes the request back into the terminal's input

**Fixed** in 0.3.130 (24a36e44): an invalid XTGETTCAP name is refused with nothing echoed; terminal_conformance checks the exact reply.

`src/blit_server.c:13243-13248`: when an XTGETTCAP name (`DCS + q ... ST`)
is not valid hex, the reply is `ESC P 0 + r` followed by the request bytes
**verbatim**. The reply is written to the PTY, which is the shell's input.
The DCS fast path keeps CR and LF (`src/blit_server.c:15385-15393` stops
only at ESC, CAN, SUB and bytes from 0x80). So `cat` of a file containing
`ESC P + q` LF `<command>` LF `ESC \` types `<command>` and Enter into the
shell. It runs as whoever owns the terminal, which is root for a
root-profile session. This is the classic title-report injection, in
another form.

Fix: answer an invalid name with `ESC P 0 + r ESC \` and nothing else, or
hex-encode what is echoed, as xterm does.

> **Ticket: XTGETTCAP echoes an invalid request back to the PTY verbatim (input injection)**
>
> `DCS + q <not hex> ST` is answered with the request's own bytes, CR and LF
> included (`blit_server.c:13243`). A program that prints a hostile file
> types its contents into the shell. Wanted: an invalid name is answered
> with an empty `DCS 0 + r ST`, or with hex only, and the reply never
> contains a byte the program sent. Gate: a terminal conformance case
> writes `ESC P + q \n echo pwned \n ESC \` and asserts the PTY receives
> exactly the empty refusal.

### SR-5 (High): the cursor can go negative

**Fixed** in 0.3.130 (757679ca): CSI parameters saturate at 65535 where they are parsed; terminal_conformance checks huge counts.

- `CSI n a` (HPR) and `CSI n e` (VPR) add `n` (up to `INT_MAX`, from
  `terminal_number`'s `(int)strtol` at `src/blit_server.c:10313`) and clamp
  only the upper bound (`src/blit_server.c:14738-14751`).
- VPA in origin mode adds `scroll_top` with no lower clamp
  (`src/blit_server.c:14936-14939`).
- Example: `ESC[2;2H ESC[2147483647a X`. `cursor_x` overflows to a negative
  value, and `terminal_put_codepoint` writes
  `surface->codepoints[cursor_y][cursor_x]` (`src/blit_server.c:15094-15095`)
  before the start of the row.

The server is root, and any program writes these bytes.

Fix: clamp CSI parameters to the screen size at parse time
(`src/blit_server.c:14174-14187`). Clamp both ends in `a`, `e` and `d`, in
64-bit or saturating arithmetic. Assert the bounds in
`terminal_put_codepoint`.

> **Ticket: HPR/VPR/VPA with a huge count make the cursor negative (out-of-bounds write)**
>
> `CSI 2147483647 a` adds the count to `cursor_x` with no overflow or lower
> clamp, and the next character is written before the start of the cell
> row. Wanted: every cursor movement leaves the cursor inside the screen
> (or the scroll region in origin mode), whatever the count. Gate: a
> terminal conformance case sends HPR, VPR, VPA (origin mode on and off),
> CUF, CUD and CHT with counts 2^31-1, 2^31 and 2^32, then a character. It
> asserts the cursor is at the right or bottom edge, and runs under ASan
> with no report.

### SR-6 (High, with a precondition): `session_json` can overflow `GET /v1/login`'s stack buffer

**Fixed** (release and commit to be filled): every member of `session_json` is written with `json_appendf`
(bounded), whole or left out, with each closer held back from where its
list or object opens, so the object is always whole JSON within its buffer;
a program's own text (title, clipboard, notification) is left out first.
`GET /v1/login`'s buffer is 64 KB like the other callers'. The precondition
holds: a login program's OSC 52 and OSC 9 reach the login session.
login_manager_gate: a login program sets a 127-quote title, a 4093-quote
clipboard and a 1023-quote notification, and `GET /v1/login` and `GET
/v1/terminal-sessions/{id}` are whole JSON carrying all three; the server
before this change answered `GET /v1/login` with 8741 bytes that were not
JSON. (No ASan build of the server exists; the bound was also checked with
the login buffer cut to 600, 2000 and 8192 bytes: whole JSON each time.)

- `session_json` appends with `used += snprintf(body + used, capacity - used, ...)`
  and no guard (`src/blit_server.c:16766-16803`).
- The escaped OSC 52 clipboard alone can be 8191 bytes (`escaped_clipboard[8192]`
  at 16720; every `"` or `\` doubles when escaped).
- `login_manager_json` passes an 8192-byte buffer
  (`src/blit_server.c:18798`, `18834`). Once `used` passes `capacity`,
  `capacity - used` wraps, and the next `snprintf` (the notification, also
  program text) writes past the end of the stack buffer.
- Precondition: the bytes have to reach the **login** session's PTY. This
  review did not confirm that path. The other callers' buffers are 64 KB
  (`src/blit_server.c:17067`) or 4 MB (`24769`), which fit.

Fix: make the buffer 64 KB like the others, and build the JSON with
`json_appendf` (bounded) everywhere in `session_json`.

> **Ticket: session_json can write past an 8 KB buffer (GET /v1/login)**
>
> `session_json` adds each field with unguarded `snprintf` arithmetic. With
> a long OSC 52 clipboard and notification on the login session,
> `login_manager_json`'s 8 KB buffer overflows. Wanted: `session_json`
> never writes past `capacity`; a field that does not fit is truncated or
> left out, and the JSON stays valid. Gate: a gate fills a session's
> clipboard (8 KB of `"`) and notification, reads `GET /v1/login` and `GET
> /v1/terminal-sessions/{id}` under ASan, and parses both as JSON.

### SR-7 (High): PTY master descriptors leak into later session children

**Fixed** in 0.3.130 (01f24d3b): PTY masters are close-on-exec; terminal_conformance checks a new session holds no other master.

`forkpty` (`src/blit_server.c:17769`) leaves the master without
`FD_CLOEXEC`. The parent only sets `O_NONBLOCK` (17928). There is no
`FD_CLOEXEC` on any PTY master in `src/`. Only the Hibr login-manager join
path closes descriptors in the child (`src/blit_server.c:17791-17802`).
Every other session child therefore inherits the masters of all the
sessions started before it. This includes a child run with `"user"` as
another account. That user can then type into root's terminals and read
their output.

Fix: set `FD_CLOEXEC` on the master right after `forkpty`, and in the child
mark everything above 2 close-on-exec (`close_range(3, ~0U,
CLOSE_RANGE_CLOEXEC)`). Then clear the flag on `inherited_fd` only.

> **Ticket: Session children inherit other sessions' PTY masters**
>
> PTY masters are not close-on-exec, and only the login-join path closes
> descriptors before `execve`. A session started as another user inherits
> writable masters of root sessions. Wanted: a session child holds only
> stdin/stdout/stderr and the descriptor it is meant to inherit. Gate: start
> two sessions, and have the second list `/proc/self/fd`. Only 0-2 (and the
> documented inherited descriptor, when one is set) are open.

### SR-8 (High, documented): the API token is given to children run as another user

`src/blit_server.c:17842-17845` sets `BLIT_API_TOKEN` in every child except
the login manager's desktop join. This happens before the drop to
`run_user` (17893-17896). ADR 0055 records that every child sees it. It
does not record what that means for a child that runs as another user:
that user holds the root API token, so the drop gains nothing.

Fix: never set the token for a child that runs as another user. Give such
children a token scoped to their own session, or nothing.

> **Ticket: Children dropped to another user receive the root API token**
>
> `BLIT_API_TOKEN` is set in every session child, including children started
> with `"user"`, so a per-user session can create root sessions. Wanted: a
> child run as another user gets no API token, or a token that cannot
> create sessions. ADR 0055 is amended to say so. Gate: start a session as
> an unprivileged user, and show its environment has no `BLIT_API_TOKEN`
> (or that `POST /v1/terminal-sessions` with its token is refused).

### SR-9 (High, accepted LAN-testing exception): the API token is a root shell, sent over plain HTTP on 0.0.0.0

- The packaged `blit.env` binds 0.0.0.0 (`packaging/build-deb.sh:320`). The
  listener is plain TCP with no TLS (`src/blit_server.c:33232`). Both tokens
  cross the LAN in clear on every request.
- With no `BLIT_COMMAND_ALLOWLIST`, every command is allowed
  (`src/blit_server.c:17104-17105`). The package sets none. The allowlist
  checks only `command`. The `user` field can name any account, and
  client-supplied environment is applied after the privilege drop
  (`src/blit_server.c:17904-17907`), so `LD_PRELOAD` and `BASH_ENV` pass
  through.
- Correct as it stands: without a token, the server binds 127.0.0.1
  instead (`src/blit_server.c:32997-33001`). It refuses an explicit network
  address with no token unless `BLIT_ALLOW_UNAUTHENTICATED_NETWORK=1`
  (33002-33017).

For the owner's test environment this is the approved exception, and it is
listed here so that it stays a decision rather than a default. Before a
release outside that environment, the default should be 127.0.0.1 (an SSH
tunnel or a TLS proxy for remote use), with a default allowlist.

> **Ticket: Release default: loopback bind, an allowlist, and no loader variables from clients**
>
> The package listens on 0.0.0.0 over plain HTTP, with no command
> allowlist, and a session request can set `LD_PRELOAD` and similar
> variables. That is the approved exception for the internal test
> environment. Wanted, for a release outside it: the generated `blit.env`
> binds 127.0.0.1, ships a `BLIT_COMMAND_ALLOWLIST`, and the server refuses
> `LD_*`, `BASH_ENV`, `ENV` and `PATH` in a session's `environment`. The LAN
> exception becomes an explicit, documented operator setting. Gate: the
> packaging gate reads the generated `blit.env` (bind and allowlist), and a
> session gate shows `environment: {"LD_PRELOAD": ...}` is refused with a
> 400.

### SR-10 (Medium): the helper reads the staged package three times by path

The helper hashes the file (`sha256sum`), reads its fields (`dpkg-deb
--field`), and installs it (`dpkg --install`). Each step opens it again by
path (`src/blit_update_helper.c:218-225`, `261`). The directory is
group-writable without the sticky bit (`packaging/build-deb.sh:382`), so a
group member can swap the file between the hash and the install. The
server's staging descriptor is not close-on-exec
(`src/blit_server.c:32480`, `mkstemp`), so a session forked during an
upload inherits a writable descriptor to the staged package.

Fix: the helper opens the file once (`O_NOFOLLOW`) and copies it into a
root-only directory. It hashes and verifies the copy and installs that.
The server uses `mkostemp(path, O_CLOEXEC)`.

> **Ticket: Update helper checks and installs the staged package by path (TOCTOU)**
>
> The staged package is hashed, inspected and installed through three
> separate opens of a path in a group-writable directory. The server's
> descriptor to it leaks into session children. Wanted: the helper installs
> exactly the bytes it verified, from a copy no one else can write, and the
> staging descriptor is close-on-exec. Gate: an update_pair case replaces
> the staged file between the request and the install (a hook in the
> gate's helper build), and the original verified bytes are installed or
> the request is refused.

### SR-11 (Medium): both tokens are compared with `strcmp`

**Fixed** (release and commit to be filled): `blit_token_equal` (src/blit_token.h) compares the API
bearer token, the update token, the Hibr owner token, the frame token and
blit-compositor's activation tokens: every byte of the secret is read, and
the time depends only on the two lengths. `frame_token_equal`, which stopped
at the shorter string, is gone. The bearer scheme name is matched in any
case. token_compare_gate.sh: unit cases (equal, first and last byte,
one short, one long, empty) and a source check that no `strcmp`, `strncmp`
or `memcmp` names a token or the Authorization header; the source before
this change had four.

- Bearer: `src/blit_server.c:24788-24789`.
- Update token: `src/blit_server.c:32075`.

`frame_token_equal` (`src/blit_frames.h:161`) already compares in constant
time. Timing a remote `strcmp` is hard but not impossible, and the token is
root.

> **Ticket: Compare the API and update tokens in constant time**
>
> Both tokens are checked with `strcmp`. Wanted: both use the
> constant-time compare `frame_token_equal` already uses, over the full
> length. Gate: a unit gate for the compare (equal, differs at first/last
> byte, different lengths), and a grep gate that no token is compared with
> `strcmp`.

### SR-12 (Medium): an install blocks the server with no timeout

- The server waits for the helper's answer with a blocking `read` and no
  timeout (`src/blit_server.c:32321-32325`), on its main loop.
- The helper runs `dpkg --install` with no timeout
  (`src/blit_update_helper.c:261`; `run_command` passes 0).
- The console watchdog allows 120 s during the install
  (`src/blit_vt.c:404`). After that it SIGKILLs the server, possibly while
  dpkg is still unpacking. systemd then restarts the server, possibly on a
  half-unpacked package.

Fix: answer the upload at once with 202 and move the helper exchange off
the main loop. Bound the helper's dpkg run, with a timeout longer than any
real install, and report it.

> **Ticket: A slow dpkg freezes the server and the watchdog kills it mid-install**
>
> The privileged install runs synchronously on the server's main loop with
> no timeout, and the console watchdog kills the server at 120 s. Wanted:
> the display and API stay live during an install. The helper's dpkg run is
> bounded, and a timeout is reported in `/v1/control/update/status`. Gate:
> a gate with a stub helper that answers after 150 s shows frames
> advancing and `GET /v1/version` answering throughout, with no watchdog
> kill.

### SR-13 (Medium): dpkg accepts `blit` N-1 alone under `blit-compositor` N

Rehearsed (`rollback_recovery_gate`, case `local-half`). Root's own `dpkg
--install blit_<N-1>.deb` with the compositor N installed succeeds. dpkg
1.22 does not enforce the reverse dependency on a downgrade, and leaves
`blit-compositor : Depends: blit (= N)` broken. `dpkg --audit` is silent;
only `apt-get check` names it. Only the helper enforces the pair (ADR
0232), so a manual rollback can break it.

Fix: `blit`'s control file says `Breaks: blit-compositor (<< ${pkgver}),
blit-compositor (>> ${pkgver})`. dpkg then refuses the half rollback, or
deconfigures the compositor.

> **Ticket: dpkg lets blit be rolled back without blit-compositor**
>
> With both at N, `dpkg --install blit_<N-1>.deb` succeeds and leaves
> blit-compositor's `Depends: blit (= N)` broken (seen in
> rollback_recovery_gate's local-half case). Wanted: `blit` declares
> `Breaks` on every other version of `blit-compositor`, so dpkg will not
> leave the pair split. Gate: rollback_recovery_gate's local-half case
> expects dpkg to refuse (the gate already accepts that outcome), and
> update_pair still passes.

### SR-14 (Medium): blit-compositor can be killed with SIGBUS through linux-dmabuf

`params_check` checks the plane size once, with `lseek`, and skips the
check if `lseek` fails (`src/blit_compositor.c:1403-1405`). It never checks
that the descriptor is a dmabuf. `dmabuf_copy` later maps it and reads it
(`src/blit_compositor.c:1321`, `1330`; also the drag icon at 2214, 2223),
with no SIGBUS guard. A client passes a memfd, shrinks it after creating
the buffer, and commits. The compositor, and every program window in the
session, dies.

> **Ticket: A memfd passed as a dmabuf can SIGBUS blit-compositor**
>
> linux-dmabuf buffers are size-checked once and later mmapped and read
> with no guard. A client that shrinks a memfd after `create` kills the
> compositor. Wanted: a descriptor that is not a dmabuf, or a memfd without
> `F_SEAL_SHRINK`, is refused at `create`, and reads are guarded as wl_shm
> reads are. Gate: a compositor_gate case creates a buffer from a memfd,
> truncates it, and commits. The client gets a protocol error and the
> compositor keeps serving another client.

### SR-15 (Medium): viewport source or buffer scale overflow in the drag-icon scaler

The source rectangle check adds `wl_fixed_t` values in 32 bits
(`src/blit_compositor.c:2471`), so `x + w` can wrap past the bound. The
buffer scale has a lower bound only (`src/blit_compositor.c:2682`). At
scale 256 or more, `v[0]` (`2495`) truncates negative, and the clamp
(`2501`) handles only the upper edge. The read at `2275-2276` then starts
before the buffer.

> **Ticket: wp_viewport source and buffer scale can make the drag icon read outside its buffer**
>
> The viewport bounds check overflows in 32 bits, and the scale is
> unbounded, so the drag-icon scaler can read before or past the wl_shm
> buffer. Wanted: the check is done in 64 bits, the scale is capped (and
> refused above the cap), and the view is clamped at both ends. Gate: a
> compositor_gate case sets a source of x=2^23-1, w=2^23-1 and a scale of
> 1024 on a drag icon. It gets a protocol error, under ASan with no report.

### SR-16 (Medium): one CHT sequence stalls the server

**Fixed** (release and commit to be filled): CHT stops once the cursor stops moving (the last
column), as CBT stops at the first, so its work is bounded by the row. SR-5's
saturation at 65535 had already removed the 2^31 loop; what was left was
65535 iterations per sequence, thousands of sequences per write.
terminal_conformance: `CSI 2147483647 I` and 8000 `CSI 65535 I` in one write,
then DSR, answered at the last column within 200 ms (4 ms); the server
before this change took 2.9 s.

`CSI 2147483647 I` runs `terminal_next_tab` that many times with no early
exit (`src/blit_server.c:14847`). CBT does stop early. This holds every
session and the display for seconds.

> **Ticket: CHT with a huge count stalls the server**
>
> `CSI n I` loops `n` times even after the cursor reaches the last column.
> Wanted: CHT stops at the right margin (or its count is clamped to the
> width). Gate: a terminal conformance case sends `CSI 2147483647 I` and
> the reply to a following DSR arrives within 50 ms.

### SR-17 (Medium): connection slots can be used up before authentication

- HTTP: 32 slots (`src/blit_server.c:94`). When they are full, a new
  connection gets 503 `http_capacity` (32034-32036). An idle client is
  dropped after 5 s. A client that keeps reconnecting holds the API, and
  with it the owner's way in.
- Frame socket: 32 slots, and nothing times out a client that never sends
  its hello (`src/blit_frames.h:1227-1233`). The socket is abstract
  (`src/blit_frames.h:1189`), so any local uid can connect.

> **Ticket: Unauthenticated connections can hold every HTTP and frame-channel slot**
>
> Wanted: a per-peer connection limit on HTTP, and a short deadline for a
> request's headers. The frame socket drops a client that has not sent a
> valid hello within 2 s. Gate: open 32 idle connections to each. An
> authenticated request still succeeds within the deadline.

### SR-18 (Low): `png_decode` leaks on some error paths

**Fixed** (release and commit to be filled): every refusal in `png_decode` leaves through one exit that
frees the compressed, raw and RGBA buffers. terminal_conformance: 50 PNGs
(o=z), each 6 MB of IDAT then a 769-byte PLTE, are all refused and the
server's RSS grows by at most 64 MB (12 MB); the server before this change
grew by 308 MB.

If an IHDR, PLTE or tRNS chunk is refused after an IDAT has been
collected, `png_decode` returns without freeing the compressed buffer
(`src/blit_server.c:11203`, `11206`, `11208`, `11212`). That can be up to
8 MB per image, outside the graphics budget, and a program can repeat it.

> **Ticket: png_decode leaks its compressed buffer when a later chunk is refused**
>
> Wanted: every return frees the buffer. Gate: a Kitty gate sends 50 PNGs
> each with an IDAT and then a bad PLTE, and the server's RSS stays flat
> (or the gate runs under LSan with no leak).

### SR-19 (Low): headers are found by `strstr` over the whole request

**Fixed** (release and commit to be filled): headers are found at the start of a line, between the
request line and the blank line, by name in any case (`request_header_value`);
`Content-Length` is a decimal number given once (two different values close
the connection); `Sec-WebSocket-Key` uses the same lookup; a request with
`Transfer-Encoding` is refused 501 `transfer_encoding_unsupported` before
its body is read. http_header_gate.rb (from network_auth_gate.sh): the token
only in the body gets 401 (was 200), lower-case `content-length:` with the
body after a pause 201 (was 400), lower-case `authorization:` 200 (was
401), a chunked
request gets 501 (was 400 with the body ignored), conflicting
Content-Lengths are not answered (was 201).

`request_header` (`src/blit_server.c:7256`) and the `Content-Length`
lookups (`32061`, `32643`) search the whole request, body included, and are
case-sensitive. `Transfer-Encoding: chunked` is ignored. A proxy and Blit
can disagree on where a request ends.

> **Ticket: Parse HTTP headers only in the header block, case-insensitively**
>
> Wanted: header lookup stops at the blank line and ignores case. A
> request with `Transfer-Encoding` is refused with 411 or 501. Gate: an HTTP
> gate sends a body containing `Authorization: Bearer <token>` without the
> header (401), `content-length:` in lower case (accepted), and a chunked
> request (refused).

### SR-20 (Low): the JSON key scanner matches keys at any depth

`json_key_cursor` (`src/blit_server.c:7275`) matches any quoted string
followed by `:`. `{"environment":{"user":"x"}}` sets the run user. The
allowlist and the exec read the same parser, so this is not a bypass today,
but every new field inherits it.

> **Ticket: JSON fields are matched at any nesting depth**
>
> Wanted: request fields are read only from the top-level object. Gate: a
> session request with `user` only inside `environment` runs as the
> server's user.

### SR-21 (Low): a failed install reports the head of dpkg's output

The helper keeps the first 384 bytes of dpkg's output
(`src/blit_update_helper.c:25`, `COMMAND_DETAIL_CAPACITY`). For a failed
configure, that is `(Reading database ... N files and directories ...`,
and the error is lost (rehearsed: `failed-configure`). The update status is
then of no use to the operator.

> **Ticket: Update failure detail shows dpkg's first lines, not its error**
>
> Wanted: the helper reports the tail of dpkg's output (its last lines
> carry the error), and `/v1/control/update/status` shows it. Gate:
> rollback_recovery_gate's failed-configure case asserts the answer names
> the failing postinst.

### SR-22 (Low): no `Origin` or `Host` check

**Fixed** (release and commit to be filled): with no token, Blit's own origin is its listener's
(`http://127.0.0.1:PORT`, `http://localhost:PORT`, `http://[::1]:PORT`), since
it serves no pages. A request with any other `Origin` gets 403
`cross_origin` (the WebSocket upgrade included), one whose `Host` is not
loopback 403 `host_not_loopback` (not checked under
`BLIT_ALLOW_UNAUTHENTICATED_NETWORK=1`), one with a body that is not
`application/json` 415 `unsupported_media_type`; docs/api.md and the
OpenAPI description say so. With a token nothing changes. http_origin_gate.rb
(from network_auth_gate.sh's tokenless server): eight refusals and no session
created; a program, the server's own origin, `localhost` and
`application/json; charset=utf-8` are still answered. The server before
this change answered all eight, created seven sessions and upgraded the
WebSocket.

With no token, the server listens on loopback only (SR-9), and any web
page in a browser on that host can POST a session (`text/plain` needs no
preflight). There is no `Origin`, `Host` or `Content-Type` check anywhere.
Low, because the packaged install always has a token.

> **Ticket: Refuse cross-origin requests when no token is set**
>
> Wanted: a request carrying `Origin`, or a `Host` other than the bind
> address, is refused, and JSON routes require `Content-Type:
> application/json`. Gate: an HTTP gate on a tokenless loopback server
> sends a session POST with `Origin: http://example.com` and gets a 403.

### SR-23 (Info)

- OSC 52 text is stored without checking its `selection;base64` form
  (`src/blit_server.c:10883-10886`). It is never answered, so a program
  cannot read the clipboard, but any program can set what the API reports.
- `X-Blit-Version` is sent on every response, a 401 included
  (`src/blit_server.c:3193`).

## Reviewed and found correct

- **Package files.** Every file is root:root, 0755 or 0644, with no setuid
  or setgid bit (`dpkg-deb -c` of 0.3.128's packages). The helper is a root
  service, not setuid. No file capabilities are set. `/etc/blit` is 0750,
  and `update.token` and `blit.env` are 0640 (SR-3 covers who that group
  is).
- **Network bind with no token** is refused, or downgraded to loopback
  (`src/blit_server.c:32995-33017`).
- **Every API route except the update upload** is behind the bearer check
  at the top of `handle_request` (`src/blit_server.c:24779`). There is no
  unauthenticated `/v1/version` or stream.
- **Privilege drop** goes `initgroups`, `setgid`, `setuid`, with each
  checked (`src/blit_server.c:17893-17896`). The login join clears its
  environment and closes descriptors (17791-17802).
- **JSON strings**: `json_unescape` (`src/blit_server.c:7366`) is bounded
  and checks surrogate pairs. HTTP reads are bounded, and a
  `Content-Length` larger than the buffer is refused
  (`src/blit_server.c:32064`).
- **Update upload**: 64 MB cap and `X-Blit-Packages` lengths must sum to
  `Content-Length` (`src/blit_server.c:32651-32656`). The helper refuses a
  path outside its staging directory (`src/blit_update_helper.c:197-203`,
  `update_pair_gate` case `outside`). Staged files are removed after every
  answer.
- **Fonts and settings**: a font is chosen by id from known profiles, so no
  client path is used. The settings write uses `O_NOFOLLOW`
  (`src/blit_server.c:23614`). A document that cannot be read is kept as
  `settings.json.broken`, not deleted (`src/blit_server.c:23996-24003`).
- **Kitty graphics**: only `t=d` (direct) is accepted
  (`src/blit_server.c:12155-12157`), so no file or shared memory is ever
  opened as root. Images are at most 4096×4096 (64 MB). Base64, zlib and PNG
  decoding are bounded.
- **Sixel**: the colour register is below 256, repeats are clamped, and
  plots are clipped.
- **Terminal strings**: OSC/APC are capped at 16 KB and DCS at 8 MB. The
  title is cut at a UTF-8 boundary. Hyperlinks are bounded. No reply
  echoes program data except XTGETTCAP (SR-4). There is no title report.
- **REP, ECH, ICH, DCH, IL, DL, SU, SD and CUP** clamp their counts.
- **Frame channel**: a 128-bit token from `getrandom`, compared in constant
  time, with the peer uid checked on every message
  (`src/blit_frames.h:161-178`, `1022`). Whole memfd frames require seals,
  are size-checked with `fstat`, and are read with `pread`. Descriptors
  arrive with `MSG_CMSG_CLOEXEC`.
- **blit-compositor**: wl_shm reads are guarded, and X property reads check
  format and length. Titles are stripped of control characters before going
  on the wire. Only the focused client can set the clipboard.
  `xwayland_shell` is hidden from ordinary clients.

## Not reviewed

- `src/blit_frames_input.h`: whether one client can reach another's
  input.
- The frame channel's STA1, DISPLAY and WINDOW handlers. Kitty placement
  limits and `graphics_make_room`.
- The X selection transfer code.
- Build hardening. tcc builds have no stack protector or `_FORTIFY_SOURCE`.
  Whether the GCC-built release binaries have them is for
  `package_artifact_gate`'s ELF checks, which this review did not re-run.

## Rollback and recovery, as rehearsed

`tools/tests/rollback_recovery_gate.sh BLIT_DEB [PREVIOUS_BLIT_DEB]` runs as
root in a throwaway container of the release environment.
`tools/release/build-release.sh` runs it after `update_pair_gate`, with a
fabricated N-1. Run by hand, it takes the real previous release:

```sh
BLIT_BUILD_ENV_ROOT=1 tools/build-env/run.sh \
    sh tools/tests/rollback_recovery_gate.sh \
    <dir>/blit_<N>-1_amd64.deb <older-dir>/blit_<N-1>-1_amd64.deb
```

Both packages must be inside the checkout, each with its `blit-compositor`
beside it. The gate uses real dpkg and the checkout's update helper at its
deployed paths. The last four cases also use the installed `blit-server`
(on the gates' mock display, bound to 127.0.0.1, with a drawing stand-in
for Hibr's login screen) and `tools/deploy/update-live.sh`, unchanged.
Throughout, an operator-edited `/etc/blit/blit.env`, the update token, and
the settings document must stay byte for byte the same, with the same owner
and mode.

Run for this review (2026-11-21), all 12 cases passing each time: N =
0.3.128 with the real 0.3.127 as N-1, and N = 0.3.128 with N-1 fabricated
(0.3.128-0, as the release build runs it). The helper was built from the
0.3.129 source; it is unchanged since 0.3.127. Negative check: with the
helper changed to pass `--refuse-downgrade` to dpkg, the gate fails at
`rollback` (still 0.3.128 after the helper answered OK; dpkg skips a
downgrade and exits 0, and the helper does not check what was installed).

| Case | From | What happens | Result |
| --- | --- | --- | --- |
| update | N-1 | the helper installs the N pair | both N |
| rollback | N | the helper installs the N-1 pair (what `update-live.sh` with N-1 does) | both N-1 |
| rollback-half | N | the helper is given `blit` N-1 alone | refused, still N |
| local-rollback | N | root runs `dpkg --install` of the N-1 pair | both N-1 |
| local-half | N | root runs `dpkg --install` of `blit` N-1 alone | dpkg takes it; `apt-get check` names the broken pin (SR-13); the N-1 pair repairs it |
| interrupted | N-1, N unpacked but not configured | `dpkg --configure -a`; or the helper with N again; or the helper with N-1 | each recovers (N, N, N-1) |
| failed-configure | N-1 | the helper installs an N whose postinst fails | the helper answers ERROR (SR-21), blit is half-configured, and the helper with N-1 recovers it |
| damaged-files | N | `blit-server` deleted, the helper and `blit-compositor` truncated | `dpkg --verify` names all three; `dpkg --install` of the N pair restores them |
| smoke-fails | N-1 running | `update-live.sh` installs an N that comes up without its display | "smoke failed", exit 1, the API still up |
| smoke-rollback | that broken N | `update-live.sh` with N-1 | verified, the display back as at N-1 (its own smoke check reports "smoke failed": see Gaps) |
| smoke-update | N-1 | `update-live.sh` with N, then a setting saved through the API | verified, smoke passed |
| server-dead | N running | `update-live.sh` installs an N whose server exits at start | it times out; `update-live.sh` with N-1 is refused (no API); `dpkg --install` of the N-1 pair and `systemctl restart blit` bring the display back, with N's setting in effect |

### Runbook

- **The API is up** (the smoke check failed, or the install failed): run
  `tools/deploy/update-live.sh HOST <N-1 blit .deb>` with N-1's packages
  and `releases/<N-1>.yaml`. The helper installs the older pair; dpkg warns
  that it is downgrading, which is expected.
- **The API is down** (the new server does not start): from a root shell on
  the host, `dpkg --install blit_<N-1>-1_amd64.deb
  blit-compositor_<N-1>-1_amd64.deb`, always both, then `systemctl restart
  blit`. The restart is needed: the postinst does not start blit, and after
  three failed starts in 60 s the unit stays failed (`StartLimitBurst=3`).
- **dpkg was interrupted**: `dpkg --configure -a` finishes it. Installing
  either pair through the helper or with dpkg also works.
- **A file is missing or damaged**: `dpkg --verify blit blit-compositor`
  lists it (its exit status stays 0, so read the output), and `dpkg
  --install` of the installed pair restores it.
- `/etc/blit/blit.env`, `/etc/blit/update.token` and
  `/var/lib/blit/settings.json` are not package files. No install, rollback
  or repair above touches them.

### Gaps (not built here)

- **No automatic rollback.** When the new server does not come up, the
  only recovery is a root shell on the host. It also needs N-1's packages,
  which the host does not keep: staged packages are deleted after every
  answer (ADR 0232), and nothing else keeps a copy. #112 proposes that the
  helper keep the previous pair and reinstall it when the update fails its
  post-install check. That needs the owner's agreement.
- **`update-live.sh` does not roll back.** When its smoke check fails, it
  exits 1 and leaves N installed. Rolling back is a second, manual run with
  N-1.
- **The rollback run's own smoke check fails a box it repaired.**
  `update-live.sh` compares with the box as it found it. After an update
  that lost the display, "as it was" is no display. So the rollback that
  brings the display back ends `smoke failed` (the broken box read no
  display ready; the repaired one reads `1 1 1 351 1`), although the
  install was verified and the box is as it was at N-1 (rehearsed:
  `smoke-rollback`). The gate accepts either ending and
  checks the box against N-1 itself. Wanted: a "recovery" mode in
  `update-live.sh`, compared with a state recorded before the failed update
  (or a stated expected display count), that passes when the box is at
  least as it was then.
- **No written recovery runbook** existed before this document; the one
  above is what the gate rehearses.
- **Not rehearsed**: a power cut during the install (the `interrupted`
  case is its deterministic stand-in, `dpkg --unpack`), systemd's own
  restart limits (the gate's stand-in restarts without limit), a real
  display (the mock DRM device stands in), and Hibr (a stand-in program
  takes the login screen's place).
