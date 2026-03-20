# Mailbox Operations

All flows assume `state = Authenticated` unless noted.
`Selected` state inherits all Authenticated-state commands.

---

## 1. SELECT — Open a Mailbox (read-write)

The server sends several untagged responses before the final tagged `OK`.
`Session` collects them into a `SelectData` snapshot and delivers it in
`on_complete`.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : Select("INBOX", on_complete)
    Session ->>  Session : GuardState(Authenticated, "SELECT") → true
    Session ->>  Session : mailbox_state_ = SelectData{}
    Session ->>  CBS     : on_send("A1 SELECT INBOX\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * 172 EXISTS\r\n
    Session ->>  Session : on_untagged → sd.exists = 172
    Server  -->> Session : * 1 RECENT\r\n
    Session ->>  Session : on_untagged → sd.recent = 1
    Server  -->> Session : * OK [UNSEEN 12] Message 12 is the first unseen\r\n
    Session ->>  Session : mailbox_state_.unseen = 12
    Server  -->> Session : * OK [UIDVALIDITY 3857529045]\r\n
    Session ->>  Session : mailbox_state_.uid_validity = 3857529045
    Server  -->> Session : * OK [UIDNEXT 4392] Predicted next UID\r\n
    Session ->>  Session : mailbox_state_.uid_next = 4392
    Server  -->> Session : * FLAGS (\Answered \Flagged \Deleted \Seen \Draft)\r\n
    Session ->>  Session : on_untagged → sd.flags = [...]
    Server  -->> Session : * OK [PERMANENTFLAGS (\Deleted \Seen \*)] Limited\r\n
    Session ->>  Session : mailbox_state_.permanent_flags = [...]
    Server  -->> Session : A1 OK [READ-WRITE] SELECT completed\r\n
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : Transition(Selected)
    Session ->>  CBS     : on_state_change(Authenticated → Selected)
    Session ->>  Client  : on_complete(TaggedResponse{OK}, SelectData{172,1,...})
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state = Selected\nselected_mailbox_ = "INBOX"
```

---

## 2. SELECT — Failed (mailbox does not exist)

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : Select("NoSuchBox", on_complete)
    Session ->>  CBS     : on_send("A1 SELECT NoSuchBox\r\n")

    Server  -->> Session : A1 NO Mailbox does not exist\r\n
    Session ->>  Session : DispatchTagged()
    Note right of Session : status is NO → no Transition()\n mailbox_state_.reset()
    Session ->>  Client  : on_complete(TaggedResponse{NO}, SelectData{})

    Note over Session : state remains Authenticated
```

---

## 3. EXAMINE — Open Mailbox (read-only)

Identical flow to `SELECT` except the tagged response carries `[READ-ONLY]`
and state transitions to `Selected` with `mailbox_state_.read_only = true`.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : Examine("Sent", on_complete)
    Session ->>  CBS     : on_send("A1 EXAMINE Sent\r\n")

    Server  -->> Session : * 55 EXISTS\r\n
    Server  -->> Session : * 0 RECENT\r\n
    Server  -->> Session : * FLAGS (\Seen \Answered)\r\n
    Server  -->> Session : * OK [READ-ONLY] Mailbox is read-only\r\n
    Session ->>  Session : mailbox_state_.read_only = true
    Server  -->> Session : A1 OK [READ-ONLY] EXAMINE completed\r\n
    Session ->>  Session : Transition(Selected)
    Session ->>  CBS     : on_state_change(Authenticated → Selected)
    Session ->>  Client  : on_complete(TaggedResponse{OK}, SelectData{read_only=true})

    Note over Session : state = Selected (read-only)
```

---

## 4. CLOSE — De-select Mailbox (implicitly expunges deleted messages)

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Close(on_complete)
    Session ->>  Session : GuardState(Selected, "CLOSE") → true
    Session ->>  CBS     : on_send("A1 CLOSE\r\n")

    Server  -->> Session : A1 OK CLOSE completed\r\n
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : selected_mailbox_.clear()
    Session ->>  Session : mailbox_state_.reset()
    Session ->>  Session : Transition(Authenticated)
    Session ->>  CBS     : on_state_change(Selected → Authenticated)
    Session ->>  Client  : on_complete(TaggedResponse{OK})

    Note over Session : state = Authenticated
```

---

## 5. CREATE / DELETE / RENAME

Short one-round-trip commands — shown together for brevity.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    rect rgb(230,245,255)
        Note over Client,Server : CREATE
        Client  ->>  Session : Create("Archives", on_complete)
        Session ->>  CBS     : on_send("A1 CREATE Archives\r\n")
        Server  -->> Session : A1 OK CREATE completed\r\n
        Session ->>  Client  : on_complete(TaggedResponse{OK})
    end

    rect rgb(230,255,230)
        Note over Client,Server : RENAME
        Client  ->>  Session : Rename("Archives", "OldMail", on_complete)
        Session ->>  CBS     : on_send("A2 RENAME Archives OldMail\r\n")
        Server  -->> Session : A2 OK RENAME completed\r\n
        Session ->>  Client  : on_complete(TaggedResponse{OK})
    end

    rect rgb(255,230,230)
        Note over Client,Server : DELETE
        Client  ->>  Session : Delete("OldMail", on_complete)
        Session ->>  CBS     : on_send("A3 DELETE OldMail\r\n")
        Server  -->> Session : A3 OK DELETE completed\r\n
        Session ->>  Client  : on_complete(TaggedResponse{OK})
    end
```

---

## 6. LIST — Enumerate Mailboxes

The server returns zero or more `* LIST` untagged lines before the tagged OK.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : List("", "*", on_complete)
    Session ->>  Session : GuardState(Authenticated, "LIST") → true
    Session ->>  CBS     : on_send("A1 LIST \"\" *\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * LIST (\HasNoChildren) "/" INBOX\r\n
    Session ->>  Session : on_untagged → boxes.push_back(MailboxInfo{INBOX})
    Server  -->> Session : * LIST (\HasNoChildren) "/" Sent\r\n
    Session ->>  Session : on_untagged → boxes.push_back(MailboxInfo{Sent})
    Server  -->> Session : * LIST (\HasNoChildren) "/" Drafts\r\n
    Session ->>  Session : on_untagged → boxes.push_back(MailboxInfo{Drafts})
    Server  -->> Session : A1 OK LIST completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, [INBOX, Sent, Drafts])

    Note over Session : state unchanged
```

---

## 7. LSUB — List Subscribed Mailboxes

Identical flow to `LIST` but uses `LSUB` command.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Client  ->>  Session : Lsub("", "*", on_complete)
    Session ->>  CBS     : on_send("A1 LSUB \"\" *\r\n")

    Server  -->> Session : * LSUB (\HasNoChildren) "/" INBOX\r\n
    Session ->>  Session : boxes.push_back(MailboxInfo{INBOX})
    Server  -->> Session : A1 OK LSUB completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, [INBOX])
```

---

## 8. STATUS — Query Mailbox Metadata Without Selecting

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Authenticated

    Client  ->>  Session : Status("INBOX", {MESSAGES, UNSEEN}, on_complete)
    Session ->>  CBS     : on_send("A1 STATUS INBOX (MESSAGES UNSEEN)\r\n")

    Server  -->> Session : * STATUS INBOX (MESSAGES 231 UNSEEN 12)\r\n
    Session ->>  Session : on_untagged → sd = StatusData{messages=231, unseen=12}
    Server  -->> Session : A1 OK STATUS completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, StatusData{231,12})
```

---

## 9. SUBSCRIBE / UNSUBSCRIBE

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    rect rgb(230,245,255)
        Note over Client,Server : SUBSCRIBE
        Client  ->>  Session : Subscribe("Newsletters", on_complete)
        Session ->>  CBS     : on_send("A1 SUBSCRIBE Newsletters\r\n")
        Server  -->> Session : A1 OK SUBSCRIBE completed\r\n
        Session ->>  Client  : on_complete(TaggedResponse{OK})
    end

    rect rgb(255,235,200)
        Note over Client,Server : UNSUBSCRIBE
        Client  ->>  Session : Unsubscribe("Newsletters", on_complete)
        Session ->>  CBS     : on_send("A2 UNSUBSCRIBE Newsletters\r\n")
        Server  -->> Session : A2 OK UNSUBSCRIBE completed\r\n
        Session ->>  Client  : on_complete(TaggedResponse{OK})
    end
```

---

## 10. CAPABILITY — Query Server Capabilities

`CAPABILITY` is valid in **any** state and has no guard.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Client  ->>  Session : Capability(on_complete)
    Session ->>  CBS     : on_send("A1 CAPABILITY\r\n")

    Server  -->> Session : * CAPABILITY IMAP4rev1 STARTTLS AUTH=PLAIN IDLE\r\n
    Session ->>  Session : on_untagged → capabilities_ updated\ncap_data collected for callback
    Server  -->> Session : A1 OK CAPABILITY completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, CapabilityData{[IMAP4rev1, STARTTLS, ...]})

    Note over Session : capabilities_ now queryable via HasCapability()
```

---

## 11. NOOP — Keep-alive / Flush Pending Responses

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected (any state valid)

    Client  ->>  Session : Noop(on_complete)
    Session ->>  CBS     : on_send("A1 NOOP\r\n")

    opt Server sends unsolicited updates
        Server  -->> Session : * 180 EXISTS\r\n
        Session ->>  Session : mailbox_state_.exists = 180
        Session ->>  CBS     : on_unsolicited — only if no pending cmd\n(here pending = NOOP, so dispatched to on_untagged)
    end

    Server  -->> Session : A1 OK NOOP completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK})
```
