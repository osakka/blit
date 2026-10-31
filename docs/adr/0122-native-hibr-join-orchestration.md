# ADR 0122: native Hibr joins own their 2-D placement and input

- Status: accepted
- Date: 2026-10-17
- Issue: [Blit #96](https://git.home.arpa/itdlabs/blit/issues/96)
- Related: [ADR 0081](0081-hibr-joins-require-a-2d-union-contract.md)

## Verified capability

The live host now has Hibr 0.99.232, ABI 16. Hibr #238 and #240 are closed;
its hold contract supports 2-D client rectangles and client-local SGR input
rewriting. The earlier statement that Hibr cannot provide vertical layout is
historical, not the current dependency status. No Hibr source is modified.

The current desktop was authenticated through Blit's login-manager PTY and
belongs to osakka's existing held session `desktop`, owner PID 239437.
Root's hold namespace has no such desktop. Do not create a new primary or
attach a root client to an unrelated namespace.

## Live operation

Move the existing login-manager PTY to the lower pane using `POST /v1/login`.
Launch one ordinary native terminal on the upper pane as osakka, executing
`desktop --join --session desktop --name blit-upper` through Hibr. Reposition
that named client with the supported `hold move desktop blit-upper -67 0`.
Hibr normalizes its union, giving the upper client `(0,0,67,240)` and the
existing primary `(67,0,67,240)`, without changing the desktop owner.

Each client has its own Blit terminal surface, not a mirror. Physical input
goes to that client's PTY in local coordinates; Hibr's native hold client
performs its own union rewrite exactly once. Do not apply the obsolete Blit
horizontal projection to these native clients as an additional transform.
The normal terminal API is used, not a new runtime adapter or shadow state.

## Known integration gap and acceptance limits

Blit currently discovers desktop roles only from recognized direct launch
arguments. The authenticated login wrapper and ordinary native join terminal
therefore report `hibr_desktop.role=none`. The existing recognized-join path
cannot attach to this unregistered login primary. This operation is evidenced
by native `hold clients` and owner identity, not a claimed API role binding.
Issue #96 must provide the explicit association/observation contract and
unify the obsolete join special-casing with the native per-client boundary.
Do not change private flags, invent a second owner, or label the metadata gap
fixed by this live setup.

The owner PID is unchanged and the 134x240 vertical union is verified. The
lower menu bar and distinct upper application content are visible. The first
and settled captures also show dark residual rectangles in the lower pane;
full relayout rendering/input acceptance is pending, not inferred from the
successful attach. The API trace verifies an upper click targets/input-routes
to the upper PTY; a delivered event alone does not prove its UI response.

This is live orchestration, not an implemented boot/login automation. A join
must not start before successful authentication. Logout, restart/reconnect,
cross-pane dragging and graphics still need their own lifecycle/visual gates.
