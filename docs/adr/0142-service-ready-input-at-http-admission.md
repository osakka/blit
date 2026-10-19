# ADR 0142: Give admitted HTTP clients one nonblocking read slice

- Status: implemented; full regression and bounded physical comparison passed
- Date: 2026-10-19
- Issues: Blit #105, #104
- Related: ADR0141

## Evidence

Physical559 bounded admission improves SDL throughput from67–71 to221–223
delivered requests/s, but a240-slot/s producer still skips154–171 offers at
the client queue, with request p95 around51–52ms. Newly admitted descriptors
are absent from the readiness snapshot already taken by the loop, so their
first read waits another turn, potentially behind another blocking present.
This is a candidate residual delay, not yet its sole proven physical cause.

The production-code socket fixture sends a complete version request before
admission, calls the real admission helper and observes no response: the
ready request is admitted but not serviced within that turn.

## Decision

Make accepted descriptors nonblocking explicitly; Linux does not inherit
`O_NONBLOCK` from the listener, as documented by
[accept(2)](https://man7.org/linux/man-pages/man2/accept.2.html).
Within the existing eight-admission budget,
give each admitted client one call to the existing request-service function.
No data or an incomplete request returns immediately; subsequent progress
uses the existing readiness loop. Complete requests use the same parser,
authentication, update, stream and response ownership as before.

Do not add a second request implementation, inline unbounded socket draining,
SDL workers or route-specific fast paths. Preserve capacity and idle-deadline
handling. Existing finite-response helpers temporarily select blocking writes
and restore the original flags; queued responses remain nonblocking.

## Verification

The admitted-ready-request regression must fail before implementation and
pass afterward. Verify both nonblocking and close-on-exec descriptor flags,
empty/partial clients, the eight-admission budget and capacity rejection.
Run the delayed-present fixture and full network/lifecycle/renderer suites,
then repeat the actual240-slot/s physical workload and all pixel/protocol
checks. Report remaining skips/latency without assuming this closes them.

The ready-request assertion is red before the change and green afterward.
The complete socket/held-scene fixture passes on software and offscreen GLES2,
and the delayed-present burst still passes. Idle path spot checks also show
variable transport setup (curl connect approximately102ms and949ms, ICMP43ms);
those are isolated observations, not a stable network baseline. Do not
attribute all remote request RTT to Blit's event loop.

Physical560 repeats the same ten-second240-slot/s workload on all three
backends. SDL/software delivers2400with0client skips, p95=32.88ms;
SDL/GLES2 delivers2394with6skips,p95=33.92ms, versus559's171/154skips
and51.76/51.45ms respectively. All bounded pixel/protocol checks pass and
native/login/pointer restore. Transport phases are now recorded separately;
post-connect exchange still includes network/response time. This is not
sustained multi-output acceptance or proof that all latency comes from
this change. Evidence: `/tmp/blit-backend-cadence-560-240hz-20261019/`.
