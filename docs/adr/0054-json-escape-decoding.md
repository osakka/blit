# 0054 — Decode JSON string escapes at the API boundary

## Context

The terminal-input API accepts JSON strings that carry raw terminal control
bytes. JSON cannot contain a literal control byte, so conforming clients send
escapes such as `\\u001b`. The previous bounded parser decoded only a subset of
short escapes and treated a Unicode escape as its literal characters. A client
request intended to send one ESC byte therefore wrote incorrect bytes to the
PTY, corrupting Hibr and terminal interactions.

## Decision

Decode JSON short escapes and `\\uXXXX` Unicode escapes in the shared bounded
string parser. Encode decoded Unicode scalar values as UTF-8, combine valid
UTF-16 surrogate pairs, and reject malformed escapes or insufficient output
capacity. The terminal-input receipt reports the decoded byte count.

## Consequences

- API clients can send VT controls using standard JSON encoding.
- The same parser behavior applies consistently to all bounded JSON string
  fields; malformed escapes fail validation instead of being silently altered.
- The PTY write boundary remains bounded at 768 decoded bytes.
- A source-level parser change requires terminal conformance and package
  gates; Hibr remains unmodified.

## Verification

`tools/tests/terminal_conformance.rb` requires
`network-terminal-input-json-escapes` and the complete existing terminal
suite. The OpenAPI schema documents the escape behavior.
