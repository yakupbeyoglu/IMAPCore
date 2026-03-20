# Message Operations

All flows require `state = Selected` unless noted.

---

## 1. FETCH — Retrieve Messages

The server returns one `* N FETCH` untagged line per requested message.
`Session` accumulates them in a shared `vector<FetchedMessage>` and
delivers the complete list in `on_complete`.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Fetch("1:3", FLAGS|BODY, {}, uid=false, on_complete)
    Session ->>  Session : GuardState(Selected, "FETCH") → true
    Session ->>  CBS     : on_send("A1 FETCH 1:3 (FLAGS BODY)\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * 1 FETCH (FLAGS (\Seen) BODY NIL)\r\n
    Session ->>  Session : on_untagged → msgs.push_back(FetchedMessage{1,...})
    Server  -->> Session : * 2 FETCH (FLAGS (\Answered) BODY NIL)\r\n
    Session ->>  Session : on_untagged → msgs.push_back(FetchedMessage{2,...})
    Server  -->> Session : * 3 FETCH (FLAGS () BODY NIL)\r\n
    Session ->>  Session : on_untagged → msgs.push_back(FetchedMessage{3,...})
    Server  -->> Session : A1 OK FETCH completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, [msg1, msg2, msg3])
```

---

## 2. UID FETCH

Same as above — `uid=true` prepends `UID` to the generated command line.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Fetch("1000:1002", ENVELOPE, {}, uid=true, on_complete)
    Session ->>  CBS     : on_send("A1 UID FETCH 1000:1002 (ENVELOPE)\r\n")
    Server  -->> Session : * 10 FETCH (UID 1000 ENVELOPE (...))\r\n
    Session ->>  Session : msgs.push_back(FetchedMessage{uid=1000,...})
    Server  -->> Session : A1 OK UID FETCH completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, [msg_uid1000])
```

---

## 3. SEARCH — Find Messages

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Search({"UNSEEN", "SINCE", "1-Jan-2024"}, uid=false, on_complete)
    Session ->>  Session : GuardState(Selected, "SEARCH") → true
    Session ->>  CBS     : on_send("A1 SEARCH UNSEEN SINCE 1-Jan-2024\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * SEARCH 3 7 12 45\r\n
    Session ->>  Session : on_untagged → sd.message_numbers = [3,7,12,45]
    Server  -->> Session : A1 OK SEARCH completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, SearchData{[3,7,12,45]})
```

---

## 4. STORE — Update Message Flags

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Store("1:5", "+FLAGS", {Seen}, uid=false, on_complete)
    Session ->>  Session : GuardState(Selected, "STORE") → true
    Session ->>  CBS     : on_send("A1 STORE 1:5 +FLAGS (\Seen)\r\n")
    Session -->> Client  : return "A1"

    opt Server echoes updated flags (FETCH responses)
        Server  -->> Session : * 1 FETCH (FLAGS (\Seen))\r\n
        Session ->>  Session : on_untagged (collected by STORE pending cmd)
        Server  -->> Session : * 2 FETCH (FLAGS (\Seen))\r\n
    end

    Server  -->> Session : A1 OK STORE completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK})
```

---

## 5. COPY — Copy Messages to Another Mailbox

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Copy("1,3,5", "Sent", uid=false, on_complete)
    Session ->>  Session : GuardState(Selected, "COPY") → true
    Session ->>  CBS     : on_send("A1 COPY 1,3,5 Sent\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : A1 OK [COPYUID 38505 1,3,5 7,8,9] COPY completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK})
```

---

## 6. EXPUNGE — Remove Deleted Messages

The server sends one `* N EXPUNGE` line per deleted message (in reverse
order). `Session` collects their sequence numbers and delivers them in
`on_complete`.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected\n(messages 3, 7, 11 flagged \\Deleted)

    Client  ->>  Session : Expunge(on_complete)
    Session ->>  Session : GuardState(Selected, "EXPUNGE") → true
    Session ->>  CBS     : on_send("A1 EXPUNGE\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * 11 EXPUNGE\r\n
    Session ->>  Session : on_untagged → nums.push_back(11)
    Server  -->> Session : * 7 EXPUNGE\r\n
    Session ->>  Session : on_untagged → nums.push_back(7)
    Server  -->> Session : * 3 EXPUNGE\r\n
    Session ->>  Session : on_untagged → nums.push_back(3)
    Server  -->> Session : A1 OK EXPUNGE completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK}, [11, 7, 3])

    Note over Session : mailbox_state_.exists decreased by server-sent EXISTS
```

---

## 7. CHECK — Request a Mailbox Checkpoint

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = Selected

    Client  ->>  Session : Check(on_complete)
    Session ->>  Session : GuardState(Selected, "CHECK") → true
    Session ->>  CBS     : on_send("A1 CHECK\r\n")

    Server  -->> Session : A1 OK CHECK completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK})
```

---

## 8. APPEND — Upload a Message (literal flow)

`APPEND` uses IMAP literal syntax. The server issues a `+` continuation
before the client may transmit the raw message bytes.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Transport as TCP / TLS Socket
    participant Server

    Note over Session : state = Authenticated (or Selected)

    Client  ->>  Session : Append("INBOX", 512, {Seen}, nullopt,\non_literal_ready, on_complete)
    Session ->>  Session : GuardState(Authenticated, "APPEND") → true
    Session ->>  Session : append_literal_cb_ = on_literal_ready
    Session ->>  CBS     : on_send("A1 APPEND INBOX (\Seen) {512}\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : + Ready for literal data\r\n
    Session ->>  Session : DispatchContinuation()
    Session ->>  Session : append_literal_cb_ set → invoke & clear
    Session ->>  Client  : on_literal_ready()

    Note over Client,Transport : Caller sends the 512 raw message bytes
    Client  ->>  Transport : write(rawMessageBytes, 512)
    Client  ->>  Transport : write("\r\n")

    Server  -->> Session : A1 OK APPEND completed\r\n
    Session ->>  Client  : on_complete(TaggedResponse{OK})
```

---

## 9. LOGOUT — Graceful Session Teardown

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : any state (no guard)

    Client  ->>  Session : Logout(on_complete)
    Session ->>  CBS     : on_send("A1 LOGOUT\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : * BYE IMAP server terminating connection\r\n
    Session ->>  Session : DispatchUntagged() → keyword BYE
    Session ->>  Session : Transition(Logout)
    Session ->>  CBS     : on_state_change(prev → Logout)

    Server  -->> Session : A1 OK LOGOUT completed\r\n
    Session ->>  Session : DispatchTagged() → CommandType::kLogout
    Session ->>  Session : Transition(Logout) [no-op — already Logout]
    Session ->>  Client  : on_complete(TaggedResponse{OK})

    Note over Client : Caller closes the TCP socket
```
