#include "imap/command.h"

#include <gtest/gtest.h>

#include <string>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// CommandBuilder tests
// ---------------------------------------------------------------------------

class CommandBuilderTest : public ::testing::Test {
 protected:
  CommandBuilder builder_{"A"};
};

TEST_F(CommandBuilderTest, CapabilityCommandFormat) {
  std::string cmd = builder_.Capability();
  EXPECT_EQ(cmd, "A1 CAPABILITY\r\n");
  EXPECT_EQ(builder_.LastTag(), "A1");
}

TEST_F(CommandBuilderTest, TagsIncrement) {
  builder_.Capability();
  builder_.Noop();
  std::string third = builder_.Logout();
  EXPECT_EQ(third, "A3 LOGOUT\r\n");
}

TEST_F(CommandBuilderTest, ResetResetsCounter) {
  builder_.Capability();
  builder_.Capability();
  builder_.Reset();
  std::string cmd = builder_.Capability();
  EXPECT_EQ(cmd, "A1 CAPABILITY\r\n");
}

TEST_F(CommandBuilderTest, LoginQuotesCredentials) {
  std::string cmd = builder_.Login("user@example.com", "p@ss\"word");
  EXPECT_EQ(cmd, "A1 LOGIN \"user@example.com\" \"p@ss\\\"word\"\r\n");
}

TEST_F(CommandBuilderTest, LoginSimpleCredentials) {
  std::string cmd = builder_.Login("alice", "secret");
  EXPECT_EQ(cmd, "A1 LOGIN \"alice\" \"secret\"\r\n");
}

TEST_F(CommandBuilderTest, SelectMailboxWithSpaceIsQuoted) {
  std::string cmd = builder_.Select("Sent Mail");
  EXPECT_EQ(cmd, "A1 SELECT \"Sent Mail\"\r\n");
}

TEST_F(CommandBuilderTest, SelectSimpleMailbox) {
  std::string cmd = builder_.Select("INBOX");
  EXPECT_EQ(cmd, "A1 SELECT INBOX\r\n");
}

TEST_F(CommandBuilderTest, ExamineMailbox) {
  std::string cmd = builder_.Examine("INBOX");
  EXPECT_EQ(cmd, "A1 EXAMINE INBOX\r\n");
}

TEST_F(CommandBuilderTest, CreateMailbox) {
  std::string cmd = builder_.Create("NewFolder");
  EXPECT_EQ(cmd, "A1 CREATE NewFolder\r\n");
}

TEST_F(CommandBuilderTest, DeleteMailbox) {
  std::string cmd = builder_.Delete("OldFolder");
  EXPECT_EQ(cmd, "A1 DELETE OldFolder\r\n");
}

TEST_F(CommandBuilderTest, RenameMailbox) {
  std::string cmd = builder_.Rename("OldName", "NewName");
  EXPECT_EQ(cmd, "A1 RENAME OldName NewName\r\n");
}

TEST_F(CommandBuilderTest, ListCommand) {
  std::string cmd = builder_.List("", "*");
  EXPECT_EQ(cmd, "A1 LIST \"\" \"*\"\r\n");
}

TEST_F(CommandBuilderTest, StatusCommand) {
  std::string cmd = builder_.Status(
      "INBOX", {StatusItem::kMessages, StatusItem::kUnseen});
  EXPECT_EQ(cmd, "A1 STATUS INBOX (MESSAGES UNSEEN)\r\n");
}

TEST_F(CommandBuilderTest, SearchCommand) {
  std::string cmd = builder_.Search({"UNSEEN", "FROM", "\"alice\""});
  EXPECT_EQ(cmd, "A1 SEARCH UNSEEN FROM \"alice\"\r\n");
}

TEST_F(CommandBuilderTest, UidSearchCommand) {
  std::string cmd = builder_.Search({"ALL"}, true);
  EXPECT_EQ(cmd, "A1 UID SEARCH ALL\r\n");
}

TEST_F(CommandBuilderTest, FetchFlagsAndEnvelope) {
  FetchItems items = FetchItem::kFlags | FetchItem::kEnvelope;
  std::string cmd = builder_.Fetch("1:*", items);
  EXPECT_NE(cmd.find("FETCH"), std::string::npos);
  EXPECT_NE(cmd.find("FLAGS"), std::string::npos);
  EXPECT_NE(cmd.find("ENVELOPE"), std::string::npos);
}

TEST_F(CommandBuilderTest, FetchBodySection) {
  BodySection sec;
  sec.section = "TEXT";
  std::string cmd =
      builder_.Fetch("1", static_cast<FetchItems>(FetchItem::kUid), {sec});
  EXPECT_NE(cmd.find("BODY[TEXT]"), std::string::npos);
}

TEST_F(CommandBuilderTest, StoreAddFlags) {
  MessageFlags flags = MessageFlag::kSeen | MessageFlag::kDeleted;
  std::string cmd = builder_.Store("1:5", "+FLAGS", flags);
  EXPECT_NE(cmd.find("+FLAGS"), std::string::npos);
  EXPECT_NE(cmd.find("\\Seen"), std::string::npos);
  EXPECT_NE(cmd.find("\\Deleted"), std::string::npos);
}

TEST_F(CommandBuilderTest, CopyCommand) {
  std::string cmd = builder_.Copy("1:3", "Archive");
  EXPECT_EQ(cmd, "A1 COPY 1:3 Archive\r\n");
}

TEST_F(CommandBuilderTest, IdleAndDone) {
  std::string idle = builder_.Idle();
  EXPECT_EQ(idle, "A1 IDLE\r\n");
  EXPECT_EQ(CommandBuilder::Done(), "DONE\r\n");
}

TEST_F(CommandBuilderTest, AppendWithLiteralSize) {
  std::string cmd = builder_.Append("INBOX", 1024);
  EXPECT_NE(cmd.find("{1024}"), std::string::npos);
  EXPECT_NE(cmd.find("APPEND"), std::string::npos);
}

TEST_F(CommandBuilderTest, AppendWithFlagsAndDate) {
  MessageFlags flags = static_cast<MessageFlags>(MessageFlag::kSeen);
  std::string cmd =
      builder_.Append("INBOX", 512, flags, "\" 7-Feb-1994 21:52:25 -0800\"");
  EXPECT_NE(cmd.find("\\Seen"), std::string::npos);
  EXPECT_NE(cmd.find("{512}"), std::string::npos);
}

// ---------------------------------------------------------------------------
// SequenceSetBuilder tests
// ---------------------------------------------------------------------------

class SequenceSetBuilderTest : public ::testing::Test {
 protected:
  SequenceSetBuilder builder_;
};

TEST_F(SequenceSetBuilderTest, SingleNumber) {
  builder_.Add(5);
  EXPECT_EQ(builder_.Build(), "5");
}

TEST_F(SequenceSetBuilderTest, MultipleNumbers) {
  builder_.Add(1).Add(3).Add(5);
  EXPECT_EQ(builder_.Build(), "1,3,5");
}

TEST_F(SequenceSetBuilderTest, Range) {
  builder_.AddRange(1, 10);
  EXPECT_EQ(builder_.Build(), "1:10");
}

TEST_F(SequenceSetBuilderTest, Wildcard) {
  builder_.AddRange(1, 0).AddWildcard();
  EXPECT_NE(builder_.Build().find('*'), std::string::npos);
}

TEST_F(SequenceSetBuilderTest, MixedSet) {
  builder_.Add(1).AddRange(3, 5).Add(10);
  EXPECT_EQ(builder_.Build(), "1,3:5,10");
}

TEST_F(SequenceSetBuilderTest, ClearResetsBuilder) {
  builder_.Add(1).Add(2);
  builder_.Clear();
  EXPECT_EQ(builder_.Build(), "");
}

}  // namespace
}  // namespace imap
