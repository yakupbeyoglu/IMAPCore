#include "imap/session.h"
#include "imap/session_transport.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace imap {
namespace {

// Compile-time verification that Session satisfies the CRTP concepts.
static_assert(kIsSessionType<Session>,
              "Session must satisfy SessionLike and SessionEventHandler");

// ---------------------------------------------------------------------------
// Test fixture — creates a Session whose "transport" writes to `sent_`.
// ---------------------------------------------------------------------------
class SessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SessionCallbacks cbs;
    cbs.on_send = [this](std::string_view data) {
      sent_ += data;
    };
    cbs.on_state_change = [this](SessionState /*old*/, SessionState new_state) {
      state_changes_.push_back(new_state);
    };
    cbs.on_response = [this](const ParsedResponse& r) {
      responses_.push_back(r);
    };
    cbs.on_unsolicited = [this](const UntaggedResponse& u) {
      unsolicited_.push_back(u);
    };
    cbs.on_error = [this](std::string_view reason) {
      errors_.push_back(std::string(reason));
    };
    session_ = std::make_unique<Session>(std::move(cbs));
  }

  // Feed a line to the session (simulates server data).
  void Feed(std::string_view line) {
    session_->Receive(std::string(line) + "\r\n");
  }

  // Advance to Authenticated state and reset sent_ so tests start clean.
  void Authenticate() {
    session_->Login("alice", "secret");
    Feed("A1 OK LOGIN completed");
    sent_.clear();
  }

  // Advance to Selected state (INBOX) and reset sent_.
  void SelectInbox() {
    Authenticate();
    session_->Select("INBOX");
    Feed("* 0 EXISTS");
    Feed("* 0 RECENT");
    Feed("A2 OK [READ-WRITE] SELECT completed");
    sent_.clear();
  }

  std::unique_ptr<Session> session_;
  std::string sent_;
  std::vector<SessionState> state_changes_;
  std::vector<ParsedResponse> responses_;
  std::vector<UntaggedResponse> unsolicited_;
  std::vector<std::string> errors_;
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(SessionTest, InitialStateIsNotAuthenticated) {
  EXPECT_EQ(session_->State(), SessionState::kNotAuthenticated);
}

// ---------------------------------------------------------------------------
// CAPABILITY command
// ---------------------------------------------------------------------------

TEST_F(SessionTest, CapabilityCommandSent) {
  session_->Capability();
  EXPECT_NE(sent_.find("CAPABILITY"), std::string::npos);
}

TEST_F(SessionTest, CapabilityCallbackInvoked) {
  bool called = false;
  CapabilityData received;
  session_->Capability([&](const TaggedResponse& tr, const CapabilityData& cap) {
    called = true;
    received = cap;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });

  Feed("* CAPABILITY IMAP4rev1 STARTTLS IDLE");
  Feed("A1 OK CAPABILITY completed");

  EXPECT_TRUE(called);
  EXPECT_EQ(received.capabilities.size(), 3u);
}

// ---------------------------------------------------------------------------
// LOGIN
// ---------------------------------------------------------------------------

TEST_F(SessionTest, LoginCommandSent) {
  session_->Login("alice", "secret");
  EXPECT_NE(sent_.find("LOGIN"), std::string::npos);
  EXPECT_NE(sent_.find("alice"), std::string::npos);
}

TEST_F(SessionTest, SuccessfulLoginTransitionsToAuthenticated) {
  bool called = false;
  session_->Login("alice", "secret", [&](const TaggedResponse& tr) {
    called = true;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });

  Feed("A1 OK LOGIN completed");

  EXPECT_TRUE(called);
  EXPECT_EQ(session_->State(), SessionState::kAuthenticated);
  ASSERT_FALSE(state_changes_.empty());
  EXPECT_EQ(state_changes_.back(), SessionState::kAuthenticated);
}

TEST_F(SessionTest, FailedLoginStaysNotAuthenticated) {
  bool called = false;
  session_->Login("alice", "wrong", [&](const TaggedResponse& tr) {
    called = true;
    EXPECT_EQ(tr.status.status, ResponseStatus::kNo);
  });

  Feed("A1 NO [AUTHENTICATIONFAILED] Invalid credentials");

  EXPECT_TRUE(called);
  EXPECT_EQ(session_->State(), SessionState::kNotAuthenticated);
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------

TEST_F(SessionTest, SelectCommandSent) {
  Authenticate();
  session_->Select("INBOX");
  EXPECT_NE(sent_.find("SELECT INBOX"), std::string::npos);
}

TEST_F(SessionTest, SuccessfulSelectTransitionsToSelected) {
  Authenticate();  // uses tag A1
  bool called = false;
  SelectData received;
  session_->Select("INBOX", [&](const TaggedResponse& tr, const SelectData& sd) {
    called = true;
    received = sd;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });

  Feed("* 172 EXISTS");
  Feed("* 1 RECENT");
  Feed("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)");
  Feed("* OK [UNSEEN 12] Message 12 is first unseen");
  Feed("* OK [UIDVALIDITY 3857529045] UIDs valid");
  Feed("* OK [UIDNEXT 4392] Predicted next UID");
  Feed("A2 OK [READ-WRITE] SELECT completed");  // A2: select is second command

  EXPECT_TRUE(called);
  EXPECT_EQ(session_->State(), SessionState::kSelected);
  EXPECT_EQ(received.exists, 172u);
  EXPECT_EQ(received.recent, 1u);
  EXPECT_EQ(received.flags.size(), 5u);
}

TEST_F(SessionTest, MailboxStateUpdatedAfterSelect) {
  Authenticate();  // A1
  session_->Select("INBOX", nullptr);  // A2

  Feed("* 10 EXISTS");
  Feed("* 2 RECENT");
  Feed("A2 OK SELECT completed");

  ASSERT_TRUE(session_->MailboxState().has_value());
  EXPECT_EQ(session_->MailboxState()->exists, 10u);
}

// ---------------------------------------------------------------------------
// SEARCH
// ---------------------------------------------------------------------------

TEST_F(SessionTest, SearchCommandSent) {
  SelectInbox();
  session_->Search({"UNSEEN"});
  EXPECT_NE(sent_.find("SEARCH UNSEEN"), std::string::npos);
}

TEST_F(SessionTest, SearchResultsDelivered) {
  SelectInbox();  // A1 login + A2 select
  bool called = false;
  SearchData received;
  session_->Search({"ALL"}, false,
                   [&](const TaggedResponse& tr, const SearchData& sd) {
                     called = true;
                     received = sd;
                     EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
                   });  // A3

  Feed("* SEARCH 1 2 5 8");
  Feed("A3 OK SEARCH completed");

  EXPECT_TRUE(called);
  ASSERT_EQ(received.numbers.size(), 4u);
  EXPECT_EQ(received.numbers[2], 5u);
}

// ---------------------------------------------------------------------------
// FETCH
// ---------------------------------------------------------------------------

TEST_F(SessionTest, FetchCommandSent) {
  SelectInbox();
  session_->Fetch("1:5", static_cast<FetchItems>(FetchItem::kFlags));
  EXPECT_NE(sent_.find("FETCH"), std::string::npos);
  EXPECT_NE(sent_.find("1:5"), std::string::npos);
}

TEST_F(SessionTest, FetchMessagesDelivered) {
  SelectInbox();  // A1 login + A2 select
  bool called = false;
  std::vector<FetchedMessage> received;
  session_->Fetch(
      "1:2", FetchItem::kFlags | FetchItem::kUid, {},
      false,
      [&](const TaggedResponse& tr, const std::vector<FetchedMessage>& msgs) {
        called = true;
        received = msgs;
        EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
      });  // A3

  Feed("* 1 FETCH (UID 10 FLAGS (\\Seen))");
  Feed("* 2 FETCH (UID 11 FLAGS ())");
  Feed("A3 OK FETCH completed");

  EXPECT_TRUE(called);
  ASSERT_EQ(received.size(), 2u);
  EXPECT_EQ(*received[0].uid, 10u);
  EXPECT_TRUE(HasFlag(received[0].flags, MessageFlag::kSeen));
  EXPECT_EQ(*received[1].uid, 11u);
}

// ---------------------------------------------------------------------------
// STORE
// ---------------------------------------------------------------------------

TEST_F(SessionTest, StoreCommandSent) {
  SelectInbox();
  MessageFlags flags = static_cast<MessageFlags>(MessageFlag::kDeleted);
  session_->Store("1:3", "+FLAGS", flags);
  EXPECT_NE(sent_.find("STORE"), std::string::npos);
  EXPECT_NE(sent_.find("+FLAGS"), std::string::npos);
  EXPECT_NE(sent_.find("\\Deleted"), std::string::npos);
}

// ---------------------------------------------------------------------------
// EXPUNGE
// ---------------------------------------------------------------------------

TEST_F(SessionTest, ExpungeCommandSent) {
  SelectInbox();
  session_->Expunge();
  EXPECT_NE(sent_.find("EXPUNGE"), std::string::npos);
}

TEST_F(SessionTest, ExpungeNumbersCollected) {
  SelectInbox();  // A1 login + A2 select
  bool called = false;
  std::vector<uint32_t> expunged;
  session_->Expunge([&](const TaggedResponse& tr,
                        const std::vector<uint32_t>& nums) {
    called = true;
    expunged = nums;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });  // A3

  Feed("* 5 EXPUNGE");
  Feed("* 3 EXPUNGE");
  Feed("A3 OK EXPUNGE completed");

  EXPECT_TRUE(called);
  ASSERT_EQ(expunged.size(), 2u);
}

// ---------------------------------------------------------------------------
// LIST
// ---------------------------------------------------------------------------

TEST_F(SessionTest, ListCommandSent) {
  Authenticate();
  session_->List("", "*");
  EXPECT_NE(sent_.find("LIST"), std::string::npos);
}

TEST_F(SessionTest, ListMailboxesCollected) {
  Authenticate();  // A1
  bool called = false;
  std::vector<MailboxInfo> boxes;
  session_->List("", "%",
                 [&](const TaggedResponse& tr,
                     const std::vector<MailboxInfo>& mb) {
                   called = true;
                   boxes = mb;
                   EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
                 });  // A2

  Feed("* LIST (\\HasNoChildren) \"/\" INBOX");
  Feed("* LIST (\\HasNoChildren) \"/\" Sent");
  Feed("A2 OK LIST completed");

  EXPECT_TRUE(called);
  ASSERT_EQ(boxes.size(), 2u);
  EXPECT_EQ(boxes[0].name, "INBOX");
  EXPECT_EQ(boxes[1].name, "Sent");
}

// ---------------------------------------------------------------------------
// LOGOUT
// ---------------------------------------------------------------------------

TEST_F(SessionTest, LogoutTransitionsToLogout) {
  bool called = false;
  session_->Logout([&](const TaggedResponse& tr) {
    called = true;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });

  Feed("* BYE Logging out");
  Feed("A1 OK LOGOUT completed");

  EXPECT_TRUE(called);
  // BYE triggers logout; tagged OK confirms.
  EXPECT_EQ(session_->State(), SessionState::kLogout);
}

// ---------------------------------------------------------------------------
// IDLE
// ---------------------------------------------------------------------------

TEST_F(SessionTest, IdleCommandSent) {
  SelectInbox();
  session_->Idle();
  EXPECT_NE(sent_.find("IDLE"), std::string::npos);
}

TEST_F(SessionTest, IdleDoneSendsDone) {
  SelectInbox();  // A1 login + A2 select
  session_->Idle();  // A3
  Feed("+ idling");  // server continuation — session is now idle
  EXPECT_TRUE(session_->IsIdle());

  session_->IdleDone();
  EXPECT_NE(sent_.find("DONE"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

TEST_F(SessionTest, HasCapabilityAfterCapabilityResponse) {
  session_->Capability();
  Feed("* CAPABILITY IMAP4rev1 IDLE STARTTLS");
  Feed("A1 OK CAPABILITY completed");

  EXPECT_TRUE(session_->HasCapability("IMAP4rev1"));
  EXPECT_TRUE(session_->HasCapability("IDLE"));
  EXPECT_FALSE(session_->HasCapability("NONEXISTENT"));
}

// ---------------------------------------------------------------------------
// NOOP
// ---------------------------------------------------------------------------

TEST_F(SessionTest, NoopCommandSent) {
  bool called = false;
  session_->Noop([&](const TaggedResponse& tr) {
    called = true;
    EXPECT_EQ(tr.status.status, ResponseStatus::kOk);
  });
  Feed("A1 OK NOOP completed");
  EXPECT_TRUE(called);
}

// ---------------------------------------------------------------------------
// Unsolicited responses
// ---------------------------------------------------------------------------

TEST_F(SessionTest, UnsolicitedExistsFiresCallback) {
  // No pending commands.
  Feed("* 15 EXISTS");
  ASSERT_FALSE(unsolicited_.empty());
  EXPECT_EQ(unsolicited_[0].keyword, "EXISTS");
}

// ---------------------------------------------------------------------------
// Multiple pipelined commands
// ---------------------------------------------------------------------------

TEST_F(SessionTest, PipelinedCommandsMatchedByTag) {
  bool noop_called = false;
  bool cap_called = false;
  session_->Noop([&](const TaggedResponse&) { noop_called = true; });
  session_->Capability([&](const TaggedResponse&, const CapabilityData&) {
    cap_called = true;
  });

  // Responses in order.
  Feed("A1 OK NOOP completed");
  Feed("* CAPABILITY IMAP4rev1");
  Feed("A2 OK CAPABILITY completed");

  EXPECT_TRUE(noop_called);
  EXPECT_TRUE(cap_called);
}

// ---------------------------------------------------------------------------
// State guard tests (RFC 3501 §3 state machine enforcement)
// ---------------------------------------------------------------------------

TEST_F(SessionTest, GuardLoginRejectsWhenAuthenticated) {
  // Bring into Authenticated state first.
  session_->Login("user", "pass");
  Feed("A1 OK LOGIN completed");

  ASSERT_EQ(session_->State(), SessionState::kAuthenticated);
  errors_.clear();
  sent_.clear();

  // Second LOGIN must be rejected locally without sending anything.
  std::string tag = session_->Login("user2", "pass2");
  EXPECT_TRUE(tag.empty());
  EXPECT_TRUE(sent_.empty());
  EXPECT_EQ(errors_.size(), 1u);
}

TEST_F(SessionTest, GuardFetchRejectsWhenNotSelected) {
  // Authenticated but no SELECT yet.
  session_->Login("user", "pass");
  Feed("A1 OK LOGIN completed");

  ASSERT_EQ(session_->State(), SessionState::kAuthenticated);
  errors_.clear();
  sent_.clear();

  std::string tag = session_->Fetch("1:*", static_cast<FetchItems>(FetchItem::kAll));
  EXPECT_TRUE(tag.empty());
  EXPECT_TRUE(sent_.empty());
  EXPECT_EQ(errors_.size(), 1u);
}

TEST_F(SessionTest, GuardSelectRejectsWhenNotAuthenticated) {
  ASSERT_EQ(session_->State(), SessionState::kNotAuthenticated);

  std::string tag = session_->Select("INBOX");
  EXPECT_TRUE(tag.empty());
  EXPECT_TRUE(sent_.empty());
  EXPECT_EQ(errors_.size(), 1u);
}

TEST_F(SessionTest, GuardSelectAllowedWhenSelected) {
  // SELECT is valid in both Authenticated and Selected state.
  session_->Login("user", "pass");
  Feed("A1 OK LOGIN completed");
  session_->Select("INBOX");
  Feed("* 0 EXISTS");
  Feed("* 0 RECENT");
  Feed("A2 OK [READ-WRITE] SELECT completed");

  ASSERT_EQ(session_->State(), SessionState::kSelected);
  errors_.clear();
  sent_.clear();

  // RE-SELECT another mailbox — still valid.
  std::string tag = session_->Select("Sent");
  EXPECT_FALSE(tag.empty());
  EXPECT_TRUE(errors_.empty());
}

TEST_F(SessionTest, GuardCommandsRejectWhenLogout) {
  // Drive session into Logout state.
  session_->Login("user", "pass");
  Feed("A1 OK LOGIN completed");
  session_->Logout();
  Feed("* BYE logging out");
  Feed("A2 OK LOGOUT completed");

  ASSERT_EQ(session_->State(), SessionState::kLogout);
  errors_.clear();
  sent_.clear();

  std::string tag = session_->Select("INBOX");
  EXPECT_TRUE(tag.empty());
  EXPECT_EQ(errors_.size(), 1u);
}

}  // namespace
}  // namespace imap
