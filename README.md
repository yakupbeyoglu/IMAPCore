# IMAPCore

IMAPCore is a high-performance, modular C++ library that implements the full IMAP protocol, designed for real-time email processing pipelines. It supports:

- **Zero-copy parsing** of server responses for maximum efficiency
- **Full IMAP support**: tagged/untagged responses, ENVELOPE, FLAGS, UID, BODY, multi-line literals, and attachments
- **Command encoding**: LOGIN, SELECT, FETCH (headers/body), IDLE, LOGOUT
- **Event-driven architecture** with observer callbacks for seamless integration into AI pipelines or message-processing systems
- **Decoupled from networking**, ready to integrate with Boost.Asio or other async TCP/SSL engines

IMAPCore is ideal for developers building real-time, automated email systems, AI-assisted mail responders, or any application that needs a production-ready IMAP client in C++.
