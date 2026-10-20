# ADR0154: retain TCC runtime fault traces

- Date:2026-10-20
- Status: implemented; candidate570, physical trace pending
- Tracking: Blit104/109

Two physical SDL multi-output startup trials create their windows then exit
SIGSEGV before the listener. Journal signal names alone cannot locate the
fault; the host has no gdb/coredumpctl and no collected core. Do not attribute
this to a font, adapter function or GPU without a trace.

Build the canonical TCC server with `-g -bt8`, the installed compiler's runtime
source/function diagnostics. Do not enable `-b` bounds instrumentation or a
second diagnostic server implementation. The same versioned binary remains
the API/package/deployment identity. Diagnostics go to service stderr/journal,
not an unauthenticated listener. No recovery from the fault is attempted inside
the signal handler; independent presentation-trial recovery remains responsible.
Compiler fault-reporting is best effort, not an async-signal-safety or kernel
hang guarantee. Existing SIGINT/SIGTERM handlers must remain functional.

Use `tcc_fault_trace_gate.rb` to prove normal exit and deliberate subprocess
fault source/function/caller reporting with the actual installed TCC. Run
schema/lifecycle gates on the canonical binary to verify restart/stop/auth
behavior. Measure performance separately; do not infer it from debug flags.

Physical trials override `Restart=no` only while the owned temporary override
exists. A single observed failure must not trigger repeated GPU initialization;
the independent timer still restores the ordinary service/restart policy.
The override fault gate verifies this property. No canonical settings writes,
Hibr changes, dependency patches or SSH.

Primary reference: installed TCC manual, Debugger options (`-g`, `-btN`, `-b`),
/usr/share/doc/tcc/tcc-doc.html. Physical crash trace and its resulting defect
regression are separate acceptance steps; this ADR does not claim SDL fixed.
