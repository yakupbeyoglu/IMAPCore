# IDLE Flow (RFC 2177)

`IDLE` is only valid in `Selected` state. It tells the server to push
unsolicited updates (EXISTS, EXPUNGE, FLAGS, etc.) without the client polling
with `NOOP`. The client terminates IDLE by calling `IdleDone()`.

---

## 1. Normal IDLE — Enter, Receive Update, Exit

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected\nis_idle_ = false

    Client  ->>  Session : Idle(on_complete)
    Session ->>  Session : GuardState(Selected, "IDLE") → true
    Session ->>  CBS     : on_send("A1 IDLE\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : + idling\r\n
    Session ->>  Session : DispatchContinuation()
    Session ->>  Session : pending_.front().type == kIdle\n→ is_idle_ = true

    Note over Session : is_idle_ = true\nServer will push real-time updates

    Server  -->> Session : * 180 EXISTS\r\n
    Session ->>  Session : DispatchUntagged()\nmailbox_state_.exists = 180
    Note right of Session : No pending on_untagged set for IDLE\n→ DoUnsolicited() fires
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{EXISTS, 180})

    Server  -->> Session : * 3 EXPUNGE\r\n
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{EXPUNGE, 3})

    Note over Client : Client decides to stop idling (e.g. wants to send FETCH)

    Client  ->>  Session : IdleDone()
    Session ->>  Session : is_idle_ → true, proceed
    Session ->>  CBS     : on_send("DONE\r\n")

    Server  -->> Session : A1 OK IDLE terminated\r\n
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : case kIdle → is_idle_ = false
    Session ->>  Client  : on_complete(TaggedResponse{OK})

    Note over Session : is_idle_ = false\nstate unchanged (Selected)
```

---

## 2. IDLE — Server Terminates (BYE during idle)

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : is_idle_ = true

    Server  -->> Session : * BYE Server shutting down\r\n
    Session ->>  Session : DispatchUntagged() → keyword BYE
    Session ->>  Session : Transition(Logout)
    Session ->>  CBS     : on_state_change(Selected → Logout)
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{BYE})

    Note over Client : on_complete for IDLE will never fire —\ncaller should detect Logout state
```

---

## 3. IdleDone() Called When Not in IDLE (no-op)

```mermaid
sequenceDiagram
    participant Client
    participant Session

    Note over Session : is_idle_ = false

    Client  ->>  Session : IdleDone()
    Session ->>  Session : is_idle_ == false → return immediately

    Note over Session : Nothing sent to server
```

---

## 4. IDLE Rejected — Wrong State

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks

    Note over Session : state = Authenticated (not Selected)

    Client  ->>  Session : Idle(on_complete)
    Session ->>  Session : GuardState(Selected, "IDLE") → false
    Session ->>  CBS     : on_error("IDLE: command not allowed\nin current session state")
    Session -->> Client  : return "" (empty tag)

    Note over Session : Nothing sent to server
```
