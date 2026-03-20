# Authentication Flows

All flows start from `NotAuthenticated` state unless noted.

---

## 1. PREAUTH Greeting (server-side pre-authentication)

The server sends `* PREAUTH` instead of `* OK`. No login command is required.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Server  -->> Session : * PREAUTH Welcome, already logged in\r\n
    Session ->>  Session : Receive(bytes)
    Session ->>  Session : DispatchUntagged()
    Session ->>  Session : Transition(Authenticated)
    Session ->>  CBS     : on_state_change(NotAuthenticated → Authenticated)
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state = Authenticated
```

---

## 2. Successful LOGIN

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Client  ->>  Session : Login("alice", "s3cr3t", on_complete)
    Session ->>  Session : GuardNotAuthenticated("LOGIN") → true
    Session ->>  CBS     : on_send("A1 LOGIN alice s3cr3t\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : A1 OK LOGIN completed\r\n
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : Transition(Authenticated)
    Session ->>  CBS     : on_state_change(NotAuthenticated → Authenticated)
    Session ->>  Client  : on_complete(TaggedResponse{OK})
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state = Authenticated
```

---

## 3. Failed LOGIN (wrong credentials)

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Client  ->>  Session : Login("alice", "wrong", on_complete)
    Session ->>  Session : GuardNotAuthenticated → true
    Session ->>  CBS     : on_send("A1 LOGIN alice wrong\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : A1 NO [AUTHENTICATIONFAILED] Invalid credentials\r\n
    Session ->>  Session : DispatchTagged()
    Note right of Session : status is NO — no Transition()
    Session ->>  Client  : on_complete(TaggedResponse{NO})
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state remains NotAuthenticated
```

---

## 4. STARTTLS then LOGIN

The client upgrades the connection to TLS before authenticating.
`STARTTLS` is only valid in `NotAuthenticated` state.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant TLS     as TLS Layer (caller)
    participant Server

    Note over Session : state = NotAuthenticated

    Client  ->>  Session : StartTls(on_tls_done)
    Session ->>  Session : GuardNotAuthenticated("STARTTLS") → true
    Session ->>  CBS     : on_send("A1 STARTTLS\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : A1 OK Begin TLS negotiation\r\n
    Session ->>  Client  : on_tls_done(TaggedResponse{OK})
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Client,TLS : Caller performs TLS handshake on the socket
    Client  ->>  TLS     : startTlsHandshake()
    TLS     -->> Client  : handshake complete

    Client  ->>  Session : Login("alice", "s3cr3t", on_login_done)
    Session ->>  Session : GuardNotAuthenticated → true
    Session ->>  CBS     : on_send("A2 LOGIN alice s3cr3t\r\n")
    Session -->> Client  : return "A2"

    Server  -->> Session : A2 OK LOGIN completed\r\n
    Session ->>  Session : Transition(Authenticated)
    Session ->>  CBS     : on_state_change(NotAuthenticated → Authenticated)
    Session ->>  Client  : on_login_done(TaggedResponse{OK})

    Note over Session : state = Authenticated
```

---

## 5. AUTHENTICATE (SASL) — e.g. PLAIN

The server sends a `+` continuation with a Base64 challenge; the client
responds with the SASL token.

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Client  ->>  Session : Authenticate("PLAIN", on_challenge, on_complete)
    Session ->>  Session : GuardNotAuthenticated → true
    Session ->>  Session : auth_challenge_cb_ = on_challenge
    Session ->>  CBS     : on_send("A1 AUTHENTICATE PLAIN\r\n")
    Session -->> Client  : return "A1"

    Server  -->> Session : + \r\n
    Note right of Server  : empty Base64 challenge
    Session ->>  Session : DispatchContinuation()
    Session ->>  Client  : on_challenge(ContinuationResponse{"+"})
    Note over Client : Client encodes credentials in Base64

    Client  ->>  CBS     : on_send("AHVzZXIAcGFzc3dvcmQ=\r\n")
    Note right of Client  : caller sends the SASL response\ndirectly via the transport

    Server  -->> Session : A1 OK AUTHENTICATE completed\r\n
    Session ->>  Session : DispatchTagged()
    Session ->>  Session : Transition(Authenticated)
    Session ->>  CBS     : on_state_change(NotAuthenticated → Authenticated)
    Session ->>  Client  : on_complete(TaggedResponse{OK})

    Note over Session : state = Authenticated
```

---

## 6. AUTHENTICATE — Server Sends Multiple Challenges

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Client  ->>  Session : Authenticate("GSSAPI", on_challenge, on_complete)
    Session ->>  CBS     : on_send("A1 AUTHENTICATE GSSAPI\r\n")

    Server  -->> Session : + YHI=\r\n
    Note right of Server  : round 1 challenge
    Session ->>  Client  : on_challenge(ContinuationResponse)
    Client  ->>  CBS     : on_send("<GSSAPI token round 1>\r\n")

    Server  -->> Session : + YHY=\r\n
    Note right of Server  : round 2 challenge
    Session ->>  Client  : on_challenge(ContinuationResponse)
    Client  ->>  CBS     : on_send("<GSSAPI token round 2>\r\n")

    Server  -->> Session : A1 OK GSSAPI authentication successful\r\n
    Session ->>  Session : Transition(Authenticated)
    Session ->>  Client  : on_complete(TaggedResponse{OK})

    Note over Session : state = Authenticated
```

---

## 7. LOGIN Guard Rejection (already authenticated)

```mermaid
sequenceDiagram
    participant Client
    participant Session
    participant CBS     as SessionCallbacks

    Note over Session : state = Authenticated

    Client  ->>  Session : Login("alice", "s3cr3t")
    Session ->>  Session : GuardNotAuthenticated("LOGIN") → false
    Session ->>  CBS     : on_error("LOGIN: command only allowed in\nNotAuthenticated state")
    Session -->> Client  : return "" (empty tag)

    Note over Session : state unchanged — nothing sent to server
```

---

## 8. BYE Greeting (server refuses connection)

```mermaid
sequenceDiagram
    participant Session
    participant CBS     as SessionCallbacks
    participant Server

    Note over Session : state = NotAuthenticated

    Server  -->> Session : * BYE Service not available\r\n
    Session ->>  Session : DispatchUntagged()
    Session ->>  Session : keyword == "BYE" → Transition(Logout)
    Session ->>  CBS     : on_state_change(NotAuthenticated → Logout)
    Session ->>  CBS     : on_response(ParsedResponse)

    Note over Session : state = Logout — caller should close socket
```
