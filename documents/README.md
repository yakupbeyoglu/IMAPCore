# IMAPCore — Session Sequence Diagrams

This directory contains Mermaid-based sequence and state diagrams that document
all observable flows through the `imap::Session` class.

---

## Contents

| File | What it covers |
|------|---------------|
| [01-state-machine.md](01-state-machine.md) | RFC 3501 §3 state machine, CRTP class architecture, internal dispatch loop |
| [02-authentication.md](02-authentication.md) | PREAUTH greeting, LOGIN, STARTTLS + LOGIN, AUTHENTICATE (SASL), failed auth |
| [03-mailbox-operations.md](03-mailbox-operations.md) | SELECT, EXAMINE, CLOSE, CREATE, DELETE, RENAME, LIST, LSUB, STATUS, SUBSCRIBE, UNSUBSCRIBE |
| [04-message-operations.md](04-message-operations.md) | CAPABILITY, NOOP, FETCH, SEARCH, STORE, COPY, EXPUNGE, APPEND (literal), CHECK, LOGOUT |
| [05-idle-flow.md](05-idle-flow.md) | IDLE (RFC 2177) — enter, unsolicited EXISTS during idle, graceful DONE exit |
| [06-error-and-guard-flows.md](06-error-and-guard-flows.md) | State guard rejections, tag mismatch, BYE / protocol errors, unsolicited responses |

---

## Key Participants

All diagrams use the same participant shorthand:

| Label | Meaning |
|-------|---------|
| `Client` | Application code calling `Session` methods |
| `Session` | `imap::Session` object |
| `Transport` | `SessionCallbacks::on_send` sink (TCP socket / TLS layer) |
| `Server` | Remote IMAP server |
| `Parser` | Internal `ResponseParser` |
| `Queue` | Internal `pending_` command queue |
| `Callbacks` | `SessionCallbacks` struct fields |
