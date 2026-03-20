# Session State Machine & Architecture

## 1. RFC 3501 §3 State Machine

```mermaid
stateDiagram-v2
    direction LR

    [*]                 --> NotAuthenticated : TCP connect\n(server greeting * OK)
    NotAuthenticated    --> Authenticated    : LOGIN / AUTHENTICATE → OK
    NotAuthenticated    --> Authenticated    : PREAUTH greeting\n(* PREAUTH ...)
    NotAuthenticated    --> Logout           : BYE greeting
    Authenticated       --> Selected         : SELECT / EXAMINE → OK
    Selected            --> Authenticated    : CLOSE → OK
    Authenticated       --> Logout           : LOGOUT → tagged OK\nor * BYE unsolicited
    Selected            --> Logout           : LOGOUT → tagged OK\nor * BYE unsolicited
    Logout              --> [*]              : connection closed
```

### Commands allowed per state

| Command | NotAuthenticated | Authenticated | Selected |
|---------|:---:|:---:|:---:|
| `CAPABILITY` | ✓ | ✓ | ✓ |
| `NOOP` | ✓ | ✓ | ✓ |
| `LOGOUT` | ✓ | ✓ | ✓ |
| `STARTTLS` | ✓ | — | — |
| `LOGIN` | ✓ | — | — |
| `AUTHENTICATE` | ✓ | — | — |
| `SELECT` / `EXAMINE` | — | ✓ | ✓ |
| `CREATE` / `DELETE` / `RENAME` | — | ✓ | ✓ |
| `SUBSCRIBE` / `UNSUBSCRIBE` | — | ✓ | ✓ |
| `LIST` / `LSUB` / `STATUS` | — | ✓ | ✓ |
| `APPEND` | — | ✓ | ✓ |
| `CHECK` / `CLOSE` | — | — | ✓ |
| `EXPUNGE` / `SEARCH` | — | — | ✓ |
| `FETCH` / `STORE` / `COPY` | — | — | ✓ |
| `IDLE` | — | — | ✓ |

> Commands sent in the wrong state are silently rejected by `GuardState` /
> `GuardNotAuthenticated` — `on_error` is invoked and nothing is sent to
> the server.

---

## 2. CRTP Class Architecture

```mermaid
classDiagram
    class SessionBase {
        <<CRTP mixin — SessionBase~Derived~>>
        #DoSend(data: string_view)
        #DoStateChange(old: SessionState, new: SessionState)
        #DoUnsolicited(ur: UntaggedResponse)
        #DoError(reason: string_view)
    }

    class Session {
        +Receive(data: string_view)
        +State() SessionState
        +SelectedMailbox() string
        +Capabilities() vector~string~
        +HasCapability(cap) bool
        +IsIdle() bool
        +MailboxState() optional~SelectData~
        ──── pre-auth commands ────
        +Capability(cb) string
        +Noop(cb) string
        +Logout(cb) string
        +StartTls(cb) string
        +Login(user, pass, cb) string
        +Authenticate(mech, challenge_cb, cb) string
        ──── auth commands ────
        +Select(mailbox, cb) string
        +Examine(mailbox, cb) string
        +Create(mailbox, cb) string
        +Delete(mailbox, cb) string
        +Rename(from, to, cb) string
        +Subscribe(mailbox, cb) string
        +Unsubscribe(mailbox, cb) string
        +List(ref, pat, cb) string
        +Lsub(ref, pat, cb) string
        +Status(mailbox, items, cb) string
        +Append(mailbox, sz, flags, dt, literal_cb, cb) string
        ──── selected commands ────
        +Check(cb) string
        +Close(cb) string
        +Expunge(cb) string
        +Search(criteria, uid, cb) string
        +Fetch(seq, items, sects, uid, cb) string
        +Store(seq, op, flags, uid, cb) string
        +Copy(seq, mailbox, uid, cb) string
        +Idle(cb) string
        +IdleDone()
        ──── CRTP hooks ────
        +OnSend(data)
        +OnStateChange(old, new)
        +OnUnsolicited(ur)
        +OnError(reason)
        ──── private ────
        -GuardState(min, cmd) bool
        -GuardNotAuthenticated(cmd) bool
        -Dispatch(resp)
        -DispatchUntagged(untagged)
        -DispatchTagged(tagged)
        -DispatchContinuation(cont)
        -EnqueueCommand(type, line, on_complete, on_untagged) string
        -Send(data)
        -Transition(new_state)
    }

    class SessionCallbacks {
        +SendCb on_send
        +ResponseCb on_response
        +StateChangeCb on_state_change
        +UnsolicitedCb on_unsolicited
        +ErrorCb on_error
    }

    class CommandBuilder {
        +Capability() string
        +Login(u, p) string
        +Authenticate(mech) string
        +Select(mb) string
        +Fetch(seq, items, sects, uid) string
        +Done() string$
        +LastTag() string
    }

    class ResponseParser {
        +Feed(data: string_view)
        +Next() optional~ParsedResponse~
    }

    class PendingCommand {
        +tag: string
        +type: CommandType
        +on_complete: TaggedCb
        +on_untagged: UntaggedCb
    }

    SessionBase      <|-- Session        : inherits (CRTP)
    Session          *--  SessionCallbacks : owns
    Session          *--  CommandBuilder   : owns
    Session          *--  ResponseParser   : owns
    Session          o--  PendingCommand   : queue (0..*)
```

---

## 3. Internal Receive / Dispatch Loop

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant Parser  as ResponseParser
    participant Queue   as pending_ queue
    participant CBS     as SessionCallbacks

    Client ->> Session : Receive(raw_bytes)
    Session ->> Parser : Feed(raw_bytes)

    loop while Parser has responses
        Session ->> Parser : Next()
        Parser -->> Session : ParsedResponse

        alt Tagged response
            Session ->> Queue : peek front PendingCommand
            Session ->> Session : Transition() if needed\n(LOGIN→Authenticated,\nSELECT→Selected, etc.)
            Session ->> CBS   : on_complete(TaggedResponse)
            Session ->> Queue : pop()
        else Untagged response
            Session ->> Session : update capabilities_\nmailbox_state_ etc.
            Session ->> Queue : front().on_untagged(untagged)
            opt No pending command owns it
                Session ->> CBS : on_unsolicited(untagged)
            end
        else Continuation response
            alt APPEND literal waiting
                Session ->> CBS : append_literal_cb_()
            else AUTHENTICATE challenge waiting
                Session ->> CBS : auth_challenge_cb_(ContResponse)
            else IDLE — server accepted
                Session ->> Session : is_idle_ = true
            end
        end

        Session ->> CBS : on_response(ParsedResponse)
    end
```

---

## 4. EnqueueCommand — Command Pipeline

```mermaid
sequenceDiagram
    participant Public  as Session public method
    participant EQ      as EnqueueCommand
    participant Builder as CommandBuilder
    participant Queue   as pending_ queue
    participant CBS     as SessionCallbacks

    Public  ->> EQ      : EnqueueCommand(type, line, on_complete, on_untagged)
    EQ      ->> Builder : LastTag()
    Builder -->> EQ     : "A7"
    EQ      ->> Queue   : push PendingCommand{tag="A7", type, callbacks}
    EQ      ->> Session : Send(line)
    Session ->> CBS     : on_send(line)
    EQ      -->> Public : return "A7"
```
