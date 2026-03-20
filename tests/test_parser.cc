#include "imap/parser.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// Helper: feed a single line and return the parsed result.
// ---------------------------------------------------------------------------
static ParsedResponse ParseOneLine(std::string_view line) {
  ResponseParser parser;
  parser.Feed(std::string(line) + "\r\n");
  auto result = parser.Next();
  EXPECT_TRUE(result.has_value());
  return *result;
}

// ---------------------------------------------------------------------------
// Greeting / Status responses
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, UntaggedOkGreeting) {
  auto resp = ParseOneLine("* OK Dovecot ready.");
  ASSERT_TRUE(resp.IsUntagged());
  const auto& u = resp.Untagged();
  EXPECT_EQ(u.keyword, "OK");
  auto* sr = std::get_if<StatusResponse>(&u.data);
  ASSERT_NE(sr, nullptr);
  EXPECT_EQ(sr->status, ResponseStatus::kOk);
  EXPECT_EQ(sr->text, "Dovecot ready.");
}

TEST(ResponseParserTest, UntaggedBye) {
  auto resp = ParseOneLine("* BYE Logging out");
  ASSERT_TRUE(resp.IsUntagged());
  EXPECT_EQ(resp.Untagged().keyword, "BYE");
}

TEST(ResponseParserTest, TaggedOk) {
  auto resp = ParseOneLine("A1 OK LOGIN completed");
  ASSERT_TRUE(resp.IsTagged());
  const auto& t = resp.Tagged();
  EXPECT_EQ(t.tag, "A1");
  EXPECT_EQ(t.status.status, ResponseStatus::kOk);
  EXPECT_EQ(t.status.text, "LOGIN completed");
}

TEST(ResponseParserTest, TaggedNo) {
  auto resp = ParseOneLine("A2 NO LOGIN failed");
  ASSERT_TRUE(resp.IsTagged());
  EXPECT_EQ(resp.Tagged().status.status, ResponseStatus::kNo);
}

TEST(ResponseParserTest, TaggedBad) {
  auto resp = ParseOneLine("A3 BAD Command unknown");
  ASSERT_TRUE(resp.IsTagged());
  EXPECT_EQ(resp.Tagged().status.status, ResponseStatus::kBad);
}

TEST(ResponseParserTest, ContinuationResponse) {
  auto resp = ParseOneLine("+ Ready for literal data");
  ASSERT_TRUE(resp.IsContinuation());
  EXPECT_EQ(resp.Continuation().text, "Ready for literal data");
}

TEST(ResponseParserTest, ContinuationEmpty) {
  auto resp = ParseOneLine("+");
  ASSERT_TRUE(resp.IsContinuation());
}

// ---------------------------------------------------------------------------
// Response codes
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, ResponseCodeReadWrite) {
  auto resp = ParseOneLine("A1 OK [READ-WRITE] SELECT completed");
  ASSERT_TRUE(resp.IsTagged());
  EXPECT_EQ(resp.Tagged().status.code.code, ResponseCode::kReadWrite);
}

TEST(ResponseParserTest, ResponseCodeUidValidity) {
  auto resp = ParseOneLine("* OK [UIDVALIDITY 12345] UIDs valid");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sr = std::get_if<StatusResponse>(&resp.Untagged().data);
  ASSERT_NE(sr, nullptr);
  EXPECT_EQ(sr->code.code, ResponseCode::kUidValidity);
  EXPECT_EQ(sr->code.arguments, "12345");
}

TEST(ResponseParserTest, ResponseCodeUidNext) {
  auto resp = ParseOneLine("* OK [UIDNEXT 4392] Predicted next UID");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sr = std::get_if<StatusResponse>(&resp.Untagged().data);
  ASSERT_NE(sr, nullptr);
  EXPECT_EQ(sr->code.code, ResponseCode::kUidNext);
}

TEST(ResponseParserTest, ResponseCodePermanentFlags) {
  auto resp =
      ParseOneLine("* OK [PERMANENTFLAGS (\\Deleted \\Seen \\*)] Flags permitted.");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sr = std::get_if<StatusResponse>(&resp.Untagged().data);
  ASSERT_NE(sr, nullptr);
  EXPECT_EQ(sr->code.code, ResponseCode::kPermanentFlags);
}

// ---------------------------------------------------------------------------
// CAPABILITY
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, CapabilityUntagged) {
  auto resp =
      ParseOneLine("* CAPABILITY IMAP4rev1 STARTTLS IDLE AUTH=PLAIN");
  ASSERT_TRUE(resp.IsUntagged());
  auto* cap = std::get_if<CapabilityData>(&resp.Untagged().data);
  ASSERT_NE(cap, nullptr);
  EXPECT_EQ(cap->capabilities.size(), 4u);
  EXPECT_EQ(cap->capabilities[0], "IMAP4rev1");
  EXPECT_EQ(cap->capabilities[2], "IDLE");
}

// ---------------------------------------------------------------------------
// FLAGS
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, FlagsUntagged) {
  auto resp = ParseOneLine("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)");
  ASSERT_TRUE(resp.IsUntagged());
  auto* fd = std::get_if<FlagsData>(&resp.Untagged().data);
  ASSERT_NE(fd, nullptr);
  EXPECT_EQ(fd->flags.size(), 5u);
}

// ---------------------------------------------------------------------------
// Numeric untagged (EXISTS, RECENT, EXPUNGE)
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, ExistsResponse) {
  auto resp = ParseOneLine("* 172 EXISTS");
  ASSERT_TRUE(resp.IsUntagged());
  const auto& u = resp.Untagged();
  ASSERT_TRUE(u.number.has_value());
  EXPECT_EQ(*u.number, 172u);
  EXPECT_EQ(u.keyword, "EXISTS");
  auto* c = std::get_if<CountData>(&u.data);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->count, 172u);
}

TEST(ResponseParserTest, RecentResponse) {
  auto resp = ParseOneLine("* 1 RECENT");
  ASSERT_TRUE(resp.IsUntagged());
  EXPECT_EQ(resp.Untagged().keyword, "RECENT");
}

TEST(ResponseParserTest, ExpungeResponse) {
  auto resp = ParseOneLine("* 3 EXPUNGE");
  ASSERT_TRUE(resp.IsUntagged());
  EXPECT_EQ(resp.Untagged().keyword, "EXPUNGE");
}

// ---------------------------------------------------------------------------
// SEARCH
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, SearchResponse) {
  auto resp = ParseOneLine("* SEARCH 2 3 6 7 11 12 18 19 20 23");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sd = std::get_if<SearchData>(&resp.Untagged().data);
  ASSERT_NE(sd, nullptr);
  EXPECT_EQ(sd->numbers.size(), 10u);
  EXPECT_EQ(sd->numbers[0], 2u);
  EXPECT_EQ(sd->numbers[9], 23u);
}

TEST(ResponseParserTest, EmptySearchResponse) {
  auto resp = ParseOneLine("* SEARCH");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sd = std::get_if<SearchData>(&resp.Untagged().data);
  ASSERT_NE(sd, nullptr);
  EXPECT_TRUE(sd->numbers.empty());
}

// ---------------------------------------------------------------------------
// STATUS
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, StatusResponse) {
  auto resp =
      ParseOneLine("* STATUS INBOX (MESSAGES 231 UIDNEXT 44292 UNSEEN 5)");
  ASSERT_TRUE(resp.IsUntagged());
  auto* sd = std::get_if<StatusData>(&resp.Untagged().data);
  ASSERT_NE(sd, nullptr);
  EXPECT_EQ(sd->mailbox, "INBOX");
  EXPECT_EQ(sd->items.at("MESSAGES"), 231u);
  EXPECT_EQ(sd->items.at("UIDNEXT"), 44292u);
  EXPECT_EQ(sd->items.at("UNSEEN"), 5u);
}

// ---------------------------------------------------------------------------
// LIST
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, ListResponse) {
  auto resp =
      ParseOneLine("* LIST (\\HasNoChildren) \"/\" INBOX");
  ASSERT_TRUE(resp.IsUntagged());
  auto* ld = std::get_if<ListData>(&resp.Untagged().data);
  ASSERT_NE(ld, nullptr);
  ASSERT_EQ(ld->mailboxes.size(), 1u);
  EXPECT_EQ(ld->mailboxes[0].name, "INBOX");
  EXPECT_EQ(ld->mailboxes[0].hierarchy_delimiter, "/");
}

TEST(ResponseParserTest, ListResponseNoSelect) {
  auto resp =
      ParseOneLine("* LIST (\\Noselect \\HasChildren) \".\" \"\"");
  ASSERT_TRUE(resp.IsUntagged());
  auto* ld = std::get_if<ListData>(&resp.Untagged().data);
  ASSERT_NE(ld, nullptr);
  ASSERT_FALSE(ld->mailboxes.empty());
  EXPECT_EQ(ld->mailboxes[0].hierarchy_delimiter, ".");
}

// ---------------------------------------------------------------------------
// FETCH
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, FetchUidAndFlags) {
  auto resp = ParseOneLine(
      "* 1 FETCH (UID 12 FLAGS (\\Seen \\Answered))");
  ASSERT_TRUE(resp.IsUntagged());
  auto* fd = std::get_if<FetchData>(&resp.Untagged().data);
  ASSERT_NE(fd, nullptr);
  ASSERT_EQ(fd->messages.size(), 1u);
  const auto& msg = fd->messages[0];
  EXPECT_EQ(msg.sequence_number, 1u);
  ASSERT_TRUE(msg.uid.has_value());
  EXPECT_EQ(*msg.uid, 12u);
  EXPECT_TRUE(HasFlag(msg.flags, MessageFlag::kSeen));
  EXPECT_TRUE(HasFlag(msg.flags, MessageFlag::kAnswered));
  EXPECT_FALSE(HasFlag(msg.flags, MessageFlag::kDeleted));
}

TEST(ResponseParserTest, FetchRfc822Size) {
  auto resp =
      ParseOneLine("* 5 FETCH (RFC822.SIZE 4242 UID 99)");
  ASSERT_TRUE(resp.IsUntagged());
  auto* fd = std::get_if<FetchData>(&resp.Untagged().data);
  ASSERT_NE(fd, nullptr);
  ASSERT_EQ(fd->messages.size(), 1u);
  EXPECT_EQ(*fd->messages[0].rfc822_size, 4242u);
  EXPECT_EQ(*fd->messages[0].uid, 99u);
}

TEST(ResponseParserTest, FetchInternalDate) {
  auto resp = ParseOneLine(
      "* 2 FETCH (INTERNALDATE \"17-Jul-1996 02:44:25 -0700\")");
  ASSERT_TRUE(resp.IsUntagged());
  auto* fd = std::get_if<FetchData>(&resp.Untagged().data);
  ASSERT_NE(fd, nullptr);
  ASSERT_TRUE(fd->messages[0].internal_date.has_value());
  EXPECT_EQ(*fd->messages[0].internal_date, "17-Jul-1996 02:44:25 -0700");
}

// ---------------------------------------------------------------------------
// Incremental feeding (multi-line)
// ---------------------------------------------------------------------------

TEST(ResponseParserTest, IncrementalFeedTwoResponses) {
  ResponseParser parser;
  parser.Feed("* 1 EXISTS\r\n");
  parser.Feed("A1 OK NOOP completed\r\n");

  auto r1 = parser.Next();
  ASSERT_TRUE(r1.has_value());
  EXPECT_TRUE(r1->IsUntagged());

  auto r2 = parser.Next();
  ASSERT_TRUE(r2.has_value());
  EXPECT_TRUE(r2->IsTagged());

  EXPECT_FALSE(parser.Next().has_value());
}

TEST(ResponseParserTest, IncrementalFeedPartialLine) {
  ResponseParser parser;
  parser.Feed("* OK Greet");
  EXPECT_FALSE(parser.Next().has_value());
  parser.Feed("ing!\r\n");
  auto r = parser.Next();
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->IsUntagged());
}

TEST(ResponseParserTest, ResetClearsBuffer) {
  ResponseParser parser;
  parser.Feed("* 1 EXISTS\r\n");
  parser.Reset();
  EXPECT_FALSE(parser.Next().has_value());
}

}  // namespace
}  // namespace imap
