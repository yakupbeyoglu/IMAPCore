# Error Handling & Guard Flows

---

## 1. State Guard — Command Not Allowed in Current State

`GuardState(min_required, cmd)` fires when the session state is below the
minimum required by a command **or** is already `Logout`.
`GuardNotAuthenticated(cmd)` fires when the session state is **not**
`NotAuthenticated` (LOGIN / AUTHENTICATE / STARTTLS are pre-auth only).

```mermaid
flowchart TD
    A([Command called]) --> B{GuardNotAuthenticated\nor GuardState?}
    B -->|GuardNotAuthenticated| C{state ==\nNotAuthenticated?}
    C -->|Yes| OK([Proceed — enqueue command])
    C -->|No| RE1[DoError: only allowed in\nNotAuthenticated state]
    RE1 --> RET([return empty tag])

    B -->|GuardState| D{state ==\nLogout?}
    D -->|Yes| RE2[DoError: session is\nin Logout state]
    RE2 --> RET

    D -->|No| E{state >=\nmin_required?}
    E -->|Yes| OK
    E -->|No| RE3[DoError: command not allowed\nin current session state]
    RE3 --> RET
```

---

## 2. Guard Scenarios — Decision Matrix

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks

    rect rgb(255,230,230)
        Note over Client,CBS : Scenario A — LOGIN when already Authenticated
        Note over Session : state = Authenticated
        Client  ->> Session : Login("u", "p")
        Session ->> Session : GuardNotAuthenticated → state != NotAuthenticated
        Session ->> CBS     : on_error("LOGIN: command only allowed\nin NotAuthenticated state")
        Session -->> Client : "" (no command sent)
    end

    rect rgb(255,230,230)
        Note over Client,CBS : Scenario B — FETCH when only Authenticated
        Note over Session : state = Authenticated
        Client  ->> Session : Fetch("1:5", FLAGS)
        Session ->> Session : GuardState(Selected, "FETCH") → Authenticated < Selected
        Session ->> CBS     : on_error("FETCH: command not allowed\nin current session state")
        Session -->> Client : ""
    end

    rect rgb(255,230,230)
        Note over Client,CBS : Scenario C — SELECT when NotAuthenticated
        Note over Session : state = NotAuthenticated
        Client  ->> Session : Select("INBOX")
        Session ->> Session : GuardState(Authenticated, "SELECT")\n→ NotAuthenticated < Authenticated
        Session ->> CBS     : on_error("SELECT: command not allowed\nin current session state")
        Session -->> Client : ""
    end

    rect rgb(255,230,230)
        Note over Client,CBS : Scenario D — Any command when Logout
        Note over Session : state = Logout
        Client  ->> Session : Noop()
        Note right of Session : NOOP has no guard — but let's show\na guarded command
        Client  ->> Session : Fetch("1", ALL)
        Session ->> Session : GuardState(Selected, "FETCH")\n→ state == Logout → special branch
        Session ->> CBS     : on_error("FETCH: session is in Logout state")
        Session -->> Client : ""
    end

    rect rgb(220,255,220)
        Note over Client,CBS : Scenario E — SELECT valid in both Authenticated and Selected
        Note over Session : state = Selected
        Client  ->> Session : Select("Sent")
        Session ->> Session : GuardState(Authenticated, "SELECT")\n→ Selected >= Authenticated ✓
        Note right of Session : Proceeds normally
    end
```

---

## 3. Tagged Response with Wrong Tag (Protocol Error)

```mermaid
sequenceDiagram
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : pending_ front = PendingCommand{tag="A1"}

    Server  -->> Session : A9 OK something\r\n
    Note right of Server  : tag "A9" does not match "A1"
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : front.tag ("A1") != tagged.tag ("A9")
    Session ->>  Session : DoError(...)
    Session -->> CBS     : on_error("Tag mismatch in tagged response")

    Note over Session : PendingCommand NOT popped — caller must handle
```

---

## 4. Unsolicited BYE (Server Terminates Mid-Session)

```mermaid
sequenceDiagram
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected\npending_ has A1, A2 enqueued

    Server  -->> Session : * BYE Server going down for maintenance\r\n
    Session ->>  Session : DispatchUntagged() → keyword "BYE"
    Session ->>  Session : Transition(Logout)
    Session ->>  CBS     : on_state_change(Selected → Logout)
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : Pending callbacks for A1, A2 will\nnever fire — caller must drain / cancel\non detecting Logout state via on_state_change
```

---

## 5. Server Sends NO or BAD to a Command

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Client  ->>  Session : Create("MyBox", on_complete)
    Session ->>  CBS     : on_send("A1 CREATE MyBox\r\n")

    Server  -->> Session : A1 NO [ALREADYEXISTS] Mailbox already exists\r\n
    Session ->>  Session : DispatchTagged()
    Note right of Session : status is NO — no state Transition
    Session ->>  Client  : on_complete(TaggedResponse{NO, ALREADYEXISTS})
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state unchanged — on_complete always fires
```

---

## 6. Unsolicited Updates While No Command is Pending

```mermaid
sequenceDiagram
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected\npending_ is EMPTY (e.g. after IDLE DONE\nbefore next command)

    Server  -->> Session : * 200 EXISTS\r\n
    Session ->>  Session : DispatchUntagged()
    Session ->>  Session : mailbox_state_.exists = 200
    Session ->>  Session : pending_ empty → DoUnsolicited()
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{EXISTS, 200})

    Server  -->> Session : * 5 EXPUNGE\r\n
    Session ->>  Session : mailbox_state_.exists updated implicitly
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{EXPUNGE, 5})

    Server  -->> Session : * FLAGS (\Answered \Flagged \Deleted \Seen \Draft)\r\n
    Session ->>  CBS     : on_unsolicited(UntaggedResponse{FLAGS, [...]})
```

---

## 7. Unsolicited Updates While a Command IS Pending

When a command is in-flight, untagged responses that arrive are delivered to
`pending_.front().on_untagged` instead of `on_unsolicited`.

```mermaid
sequenceDiagram
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : pending_ front = PendingCommand{FETCH, on_untagged}

    Server  -->> Session : * 10 EXISTS\r\n
    Session ->>  Session : DispatchUntagged()
    Session ->>  Session : mailbox_state_.exists = 10
    Session ->>  Session : pending_.front().on_untagged(untagged)
    Note right of Session : on_unsolicited NOT called — command owns it

    Server  -->> Session : * 2 FETCH (...)\r\n
    Session ->>  Session : on_untagged → msgs collected

    Server  -->> Session : A1 OK FETCH completed\r\n
    Session ->>  Session : DispatchTagged() → pop A1
    Session ->>  CBS     : on_complete(TaggedResponse, msgs)
```

---

## 8. Command Pipeline — Multiple In-Flight Commands

IMAP allows pipelining (sending the next command before the previous tagged
response arrives). `Session` maintains a FIFO queue and dispatches each
tagged response to the head of the queue.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : Create("Foo", on_create)
    Session ->>  CBS     : on_send("A1 CREATE Foo\r\n")

    Client  ->>  Session : Create("Bar", on_create2)
    Session ->>  CBS     : on_send("A2 CREATE Bar\r\n")

    Client  ->>  Session : List("", "*", on_list)
    Session ->>  CBS     : on_send("A3 LIST \"\" *\r\n")

    Note over Session : pending_ queue = [A1, A2, A3]

    Server  -->> Session : A1 OK CREATE completed\r\n
    Session ->>  Client  : on_create(TaggedResponse{A1,OK})
    Note over Session : pending_ queue = [A2, A3]

    Server  -->> Session : A2 OK CREATE completed\r\n
    Session ->>  Client  : on_create2(TaggedResponse{A2,OK})
    Note over Session : pending_ queue = [A3]

    Server  -->> Session : * LIST (\HasNoChildren) "/" Foo\r\n
    Session ->>  Session : on_untagged for A3 → boxes.push_back(Foo)
    Server  -->> Session : * LIST (\HasNoChildren) "/" Bar\r\n
    Session ->>  Session : boxes.push_back(Bar)
    Server  -->> Session : A3 OK LIST completed\r\n
    Session ->>  Client  : on_list(TaggedResponse{A3,OK}, [Foo, Bar])
    Note over Session : pending_ queue = []
```
