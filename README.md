# IMAPCore

A complete C++23 IMAP protocol library (RFC 3501 + RFC 2177 IDLE + RFC 4315
UIDPLUS).  The library ingests raw IMAP server data, parses it into strongly
typed C++ structures, and provides a full client-side control layer that is
completely decoupled from any socket or I/O implementation.

## Features

- **Full RFC 3501 command set** — CAPABILITY, NOOP, LOGOUT, STARTTLS,
  AUTHENTICATE, LOGIN, SELECT, EXAMINE, CREATE, DELETE, RENAME, SUBSCRIBE,
  UNSUBSCRIBE, LIST, LSUB, STATUS, APPEND, CHECK, CLOSE, EXPUNGE, SEARCH,
  FETCH, STORE, COPY + UID variants
- **RFC 2177 IDLE** support (IDLE / DONE)
- **Incremental pull-based response parser** — feed arbitrary byte chunks,
  pull complete `ParsedResponse` objects; handles IMAP literals `{N}`
  transparently
- **RFC 2822 / MIME message parser** — header unfolding, multipart tree,
  base64 and quoted-printable decoding
- **Session state machine** — tracks `NOT_AUTHENTICATED` → `AUTHENTICATED` →
  `SELECTED` → `LOGOUT`, maintains capability list, mailbox state snapshot,
  pipelined command queue with per-command callbacks
- **Transport agnostic** — provide `on_send` / `Receive()` hooks for any
  socket layer (plain TCP, TLS, Boost.Asio, libuv, …)


## Project structure

```
IMAPCore/
├── CMakeLists.txt
├── include/imap/
│   ├── imap.h          # Single-include umbrella header
│   ├── types.h         # Enums, flag masks, structs (SequenceSet, Envelope, …)
│   ├── message.h       # RFC 2822 / MIME message + MessageParser
│   ├── command.h       # CommandBuilder + SequenceSetBuilder
│   ├── response.h      # Typed response data structures
│   ├── parser.h        # ResponseParser (incremental)
│   └── session.h       # Session state machine
├── src/
│   ├── message.cc
│   ├── command.cc
│   ├── response.cc
│   ├── parser.cc
│   └── session.cc
└── tests/
    ├── CMakeLists.txt
    ├── test_command.cc
    ├── test_parser.cc
    ├── test_message.cc
    └── test_session.cc
```

## Requirements

- CMake ≥ 3.25
- GCC ≥ 13 or Clang ≥ 17 (C++23 `std::format`, `std::from_chars`)
- Internet access for the first build (GoogleTest fetched via `FetchContent`)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run tests

```bash
cd build && ctest --output-on-failure
```

## Usage example

```cpp
#include "imap/imap.h"

// Plug in your transport here.
imap::SessionCallbacks cbs;

cbs.on_send = [&](std::string_view data) {
    my_socket.write(data);  // send to IMAP server
};

cbs.on_state_change = [](imap::SessionState, imap::SessionState new_state) {
    // React to authentication / selection transitions.
};

cbs.on_unsolicited = [](const imap::UntaggedResponse& u) {
    // EXISTS / RECENT / EXPUNGE pushed by the server.
};

imap::Session session(std::move(cbs));

// Drive the session from received data.
// (call this whenever bytes arrive from the socket)
// session.Receive(raw_bytes_from_server);

// --- Login ---
session.Login("user@example.com", "password",
    [](const imap::TaggedResponse& r) {
        if (r.status.status == imap::ResponseStatus::kOk)
            // proceed
    });

// --- Select a mailbox ---
session.Select("INBOX",
    [](const imap::TaggedResponse& r, const imap::SelectData& sd) {
        // sd.exists, sd.uid_validity, sd.flags, …
    });

// --- Fetch messages ---
imap::SequenceSetBuilder ss;
ss.AddRange(1, 10);

session.Fetch(ss.Build(),
    imap::FetchItem::kEnvelope | imap::FetchItem::kFlags | imap::FetchItem::kUid,
    {},
    false,
    [](const imap::TaggedResponse&, const std::vector<imap::FetchedMessage>& msgs) {
        for (const auto& m : msgs) {
            if (m.envelope) {
                // m.envelope->subject, m.envelope->from, …
            }
        }
    });

// --- Parse a raw RFC 2822 message ---
imap::Message msg = imap::MessageParser::Parse(raw_rfc2822_bytes);
// msg.subject, msg.from, msg.body.parts[0].decoded_body, …

// --- IDLE ---
session.Idle([](const imap::TaggedResponse&) { /* done */ });
// … later …
session.IdleDone();
```

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  Your application / adapter                                    │
│  (any socket: BSD sockets, Boost.Asio, libuv, mock, …)        │
└───────┬─────────────────────────────────────────┬─────────────┘
        │ raw bytes in          commands out       │
        ▼                                          ▼
┌───────────────┐   ParsedResponse   ┌─────────────────────────┐
│ ResponseParser│ ──────────────────▶│       Session           │
│  (pull-based) │                    │  (RFC 3501 state machine)│
└───────────────┘                    │  CommandBuilder         │
                                     └─────────────────────────┘
        ▲
        │  raw RFC 2822 bytes
┌───────────────┐
│ MessageParser │  →  Message (MIME tree, decoded bodies)
└───────────────┘
```

## Transport adaptation pattern

IMAPCore never opens sockets.  To integrate with an async framework:

1. On socket readable → call `session.Receive(bytes)`.
2. In `on_send` callback → write the bytes to your socket.
3. In `on_error` / `on_state_change` → manage reconnect / TLS upgrade logic.

For STARTTLS: issue `session.StartTls()`, on `OK` perform the TLS handshake
in your transport layer, then call `session.Capability()` to refresh the
capability list.

## License

See [LICENSE](LICENSE).


IMAPCore is a high-performance, modular C++ library that implements the full IMAP protocol, designed for real-time email processing pipelines. It supports:

- **Zero-copy parsing** of server responses for maximum efficiency
- **Full IMAP support**: tagged/untagged responses, ENVELOPE, FLAGS, UID, BODY, multi-line literals, and attachments
- **Command encoding**: LOGIN, SELECT, FETCH (headers/body), IDLE, LOGOUT
- **Event-driven architecture** with observer callbacks for seamless integration into AI pipelines or message-processing systems
- **Decoupled from networking**, ready to integrate with Boost.Asio or other async TCP/SSL engines

IMAPCore is ideal for developers building real-time, automated email systems, AI-assisted mail responders, or any application that needs a production-ready IMAP client in C++.
