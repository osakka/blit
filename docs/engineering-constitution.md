# Engineering constitution

These are project constraints, not aspirations. A change that violates one of
them requires an explicit, accepted ADR before implementation.

## 1. One source of truth

Every important fact has one canonical owner.

- Public behavior is defined by the versioned schema and its normative
  documentation.
- Runtime state has one authoritative state engine.
- Generated artifacts are derived outputs and are never hand-edited as a
  second specification.
- Configuration, defaults, presets, and capability declarations must not be
  duplicated across transports, clients, renderers, or documentation.

When two representations are necessary, one must be explicitly identified as
the source and the other as a generated, cached, or projected form.

The repository is also the source of truth for engineering history: a
behavioral decision belongs in an ADR, an implemented contract belongs in the
schema and normative documentation, and a validation claim belongs in the
verification ledger. See [the development workflow](development-workflow.md).

## 2. No parallel implementations

There is one implementation of each semantic capability. Adapters may expose
that capability through different transports or platforms, but they must
delegate to the same core behavior.

Examples:

- one protocol model, with generated bindings for clients;
- one terminal-state engine, not separate state machines per transport;
- one effect definition and evaluation contract, not hand-maintained copies
  in multiple renderers;
- one error taxonomy, translated only at protocol boundaries.

Platform-specific code is allowed only where the platform boundary genuinely
requires it, and the boundary must be narrow and documented.

## 3. Surgical precision

Changes should be the smallest complete change that improves the system.

- Establish the invariant before editing code.
- Touch only the files and interfaces required by the change.
- Avoid speculative abstractions and broad refactors bundled with feature work.
- Preserve unrelated user work and existing behavior unless the change
  explicitly updates the contract.
- Remove ambiguity rather than adding workaround layers.

Small does not mean shallow: a surgical change must include the tests,
validation, documentation, and migration detail needed to make it complete.

## 4. Raise the bar

Every accepted design should improve at least one of correctness,
observability, determinism, security, operability, or maintainability without
silently weakening another.

The project prefers:

- explicit failure over hidden fallback;
- deterministic behavior over convenient magic;
- measured capability over marketing claims;
- boring, inspectable boundaries over clever coupling;
- evidence from conformance tests and diagnostics over confidence by analogy.

## Review gate

Before a change is considered complete, reviewers should be able to answer
yes to all of these questions:

1. Is the source of truth clear?
2. Is there exactly one semantic implementation of the capability?
3. Is the change scoped narrowly and completely?
4. Are failure modes, compatibility, and observability addressed?
5. Does the result raise the project’s quality bar?

If the answer is no, the work is not ready to merge; if the issue is a design
choice rather than an implementation defect, record it as an ADR first.

## 6. Change hygiene

Every coherent implementation cycle is tested, documented, committed, and
pushed as one traceable unit. The commit contains only intended files and the
verification ledger names the exact artifact tested. Failed experiments remain
visible as diagnostics; they are not silently folded into a later success.
