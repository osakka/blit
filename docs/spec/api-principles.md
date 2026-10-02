# API and schema principles

The schema is the single source of truth for the public contract. There must
be one semantic implementation behind all supported transports and generated
bindings. A transport adapter, client SDK, or renderer may translate the
contract, but may not redefine it.

## Schema-first workflow

The schema is the source of truth for the public contract:

1. Define or change resources, commands, events, and errors in the schema.
2. Review compatibility, examples, and behavior in documentation.
3. Generate protocol types, validators, and reference material.
4. Implement server behavior and conformance tests.
5. Version the schema and record a decision for breaking changes.

Generated code must not become the canonical place where API behavior is
defined.

## Required contract concepts

The initial schema should eventually cover:

- server identity, API version, and capabilities;
- outputs, modes, topology, hotplug, and presentation state;
- canvases, surfaces, layers, and z-order;
- text cells, styles, cursor state, and incremental updates;
- effect definitions, parameters, presets, and effect graphs;
- input devices, focus, routing, and input events;
- frame requests, acknowledgements, timing, and damage;
- subscriptions, event sequencing, and replay/resynchronization;
- typed errors and diagnostic details;
- authentication, authorization, and client leases.

## Compatibility rules

- Every request, response, event, and error has an explicit version.
- Unknown fields must be safely ignorable where possible.
- Enum evolution must define the unknown-value behavior.
- Resource identifiers are opaque to clients.
- Mutations support idempotency keys when retries could duplicate effects.
- Events carry a sequence or revision so clients can detect gaps.
- Capability discovery precedes use of optional features.
- A schema change is breaking only when an existing valid client can no longer
  safely interpret or complete its operation.

## Transport independence

The schema defines messages and semantics, not sockets, ports, framing, or
serialization libraries. Candidate transports may include a Unix-domain local
transport and a network transport, but those are implementation decisions until
their security and operational requirements are documented.

## Error model

Errors should distinguish at least:

- malformed or schema-invalid requests;
- unsupported capability or effect;
- stale revision or conflicting mutation;
- authorization failure;
- unavailable output or display resource;
- renderer or presentation failure;
- rate, memory, or frame-budget exhaustion.

Each error should include a stable code, human-readable context, and enough
structured detail for a client to recover without parsing prose.

## Determinism and effects

Effects should accept an explicit time/frame context when reproducibility is
important. The schema should distinguish static parameters from runtime inputs
such as elapsed time, frame number, noise seed, and display scale. Presets are
named schema resources rather than undocumented renderer constants.

## Open schema decisions

These are intentionally not decided yet:

- serialization format and wire framing;
- request/response versus command/event-only interaction;
- whether the first schema is OpenAPI, Protocol Buffers, JSON Schema, or a
  purpose-built combination;
- text encoding and grapheme segmentation policy;
- atomicity guarantees for multi-output presentation;
- whether surfaces may carry bitmap assets in addition to text.
