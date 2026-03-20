#include "imap/message_parser.h"

#include <gtest/gtest.h>

#include <string>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// Simple plain-text message
// ---------------------------------------------------------------------------

static constexpr std::string_view kSimplePlain =
    "Date: Mon, 7 Feb 1994 21:52:25 -0800 (PST)\r\n"
    "From: Fred Foobar <foobar@Blort.Example>\r\n"
    "Subject: afternoon meeting\r\n"
    "To: mooch@owatagu.siam.edu\r\n"
    "Message-Id: <B27397-0100000@Blort.Example>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=US-ASCII\r\n"
    "\r\n"
    "Hello Joe, do you think we can meet at 3:30 tomorrow?\r\n";

TEST(MessageParserTest, ParsesDateAndSubject) {
  Message msg = MessageParser::Parse(kSimplePlain);
  ASSERT_TRUE(msg.date.has_value());
  EXPECT_EQ(*msg.date, "Mon, 7 Feb 1994 21:52:25 -0800 (PST)");
  ASSERT_TRUE(msg.subject.has_value());
  EXPECT_EQ(*msg.subject, "afternoon meeting");
}

TEST(MessageParserTest, ParsesFromAndTo) {
  Message msg = MessageParser::Parse(kSimplePlain);
  ASSERT_TRUE(msg.from.has_value());
  EXPECT_NE(msg.from->find("Fred Foobar"), std::string::npos);
  ASSERT_TRUE(msg.to.has_value());
  EXPECT_NE(msg.to->find("mooch@owatagu.siam.edu"), std::string::npos);
}

TEST(MessageParserTest, ParsesMessageId) {
  Message msg = MessageParser::Parse(kSimplePlain);
  ASSERT_TRUE(msg.message_id.has_value());
  EXPECT_EQ(*msg.message_id, "<B27397-0100000@Blort.Example>");
}

TEST(MessageParserTest, PlainBodyContent) {
  Message msg = MessageParser::Parse(kSimplePlain);
  EXPECT_NE(msg.body.decoded_body.find("Hello Joe"), std::string::npos);
}

TEST(MessageParserTest, BodyContentType) {
  Message msg = MessageParser::Parse(kSimplePlain);
  EXPECT_EQ(msg.body.content_type, "text/plain");
}

TEST(MessageParserTest, BodyCharset) {
  Message msg = MessageParser::Parse(kSimplePlain);
  EXPECT_EQ(msg.body.charset, "us-ascii");
}

TEST(MessageParserTest, RawIsPreserved) {
  Message msg = MessageParser::Parse(kSimplePlain);
  EXPECT_EQ(msg.raw, kSimplePlain);
}

// ---------------------------------------------------------------------------
// Base64-encoded body
// ---------------------------------------------------------------------------

static constexpr std::string_view kBase64Message =
    "From: alice@example.com\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "SGVsbG8gV29ybGQ=\r\n";  // "Hello World"

TEST(MessageParserTest, DecodesBase64Body) {
  Message msg = MessageParser::Parse(kBase64Message);
  EXPECT_EQ(msg.body.decoded_body, "Hello World");
}

// ---------------------------------------------------------------------------
// Quoted-printable-encoded body
// ---------------------------------------------------------------------------

static constexpr std::string_view kQpMessage =
    "From: bob@example.com\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n"
    "\r\n"
    "Hello=20World\r\n";  // "Hello World"

TEST(MessageParserTest, DecodesQuotedPrintableBody) {
  Message msg = MessageParser::Parse(kQpMessage);
  EXPECT_NE(msg.body.decoded_body.find("Hello World"), std::string::npos);
}

TEST(MessageParserTest, QpSoftLineBreak) {
  static constexpr std::string_view kQpSoftBreak =
      "From: x@x.com\r\n"
      "Content-Transfer-Encoding: quoted-printable\r\n"
      "\r\n"
      "Long line that is =\r\n"
      "wrapped here.\r\n";
  Message msg = MessageParser::Parse(kQpSoftBreak);
  EXPECT_NE(msg.body.decoded_body.find("Long line that is wrapped here."),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Multipart message
// ---------------------------------------------------------------------------

static constexpr std::string_view kMultipart =
    "From: multipart@example.com\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"frontier\"\r\n"
    "\r\n"
    "--frontier\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "This is the plain text part.\r\n"
    "--frontier\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<p>This is the HTML part.</p>\r\n"
    "--frontier--\r\n";

TEST(MessageParserTest, MultipartParsesChildren) {
  Message msg = MessageParser::Parse(kMultipart);
  EXPECT_EQ(msg.body.content_type, "multipart/mixed");
  ASSERT_EQ(msg.body.parts.size(), 2u);
}

TEST(MessageParserTest, MultipartPartContentTypes) {
  Message msg = MessageParser::Parse(kMultipart);
  EXPECT_EQ(msg.body.parts[0].content_type, "text/plain");
  EXPECT_EQ(msg.body.parts[1].content_type, "text/html");
}

TEST(MessageParserTest, MultipartPartBodies) {
  Message msg = MessageParser::Parse(kMultipart);
  EXPECT_NE(msg.body.parts[0].decoded_body.find("plain text part"),
            std::string::npos);
  EXPECT_NE(msg.body.parts[1].decoded_body.find("HTML part"),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Header folding (RFC 2822 §2.2.3)
// ---------------------------------------------------------------------------

TEST(MessageParserTest, FoldedSubjectUnfolded) {
  static constexpr std::string_view kFolded =
      "Subject: This is a very long subject\r\n"
      " that is folded across two lines\r\n"
      "\r\n"
      "Body here.\r\n";
  Message msg = MessageParser::Parse(kFolded);
  ASSERT_TRUE(msg.subject.has_value());
  EXPECT_NE(msg.subject->find("that is folded"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(MessageParserTest, NoBodyMessage) {
  static constexpr std::string_view kNoBody =
      "From: nobody@example.com\r\n"
      "Subject: empty\r\n"
      "\r\n";
  // Should not throw.
  EXPECT_NO_THROW({ Message msg = MessageParser::Parse(kNoBody); });
  Message msg = MessageParser::Parse(kNoBody);
  EXPECT_TRUE(msg.body.decoded_body.empty());
}

TEST(MessageParserTest, LfOnlyLineEndings) {
  static constexpr std::string_view kLf =
      "From: lf@example.com\n"
      "Subject: LF only\n"
      "\n"
      "LF body\n";
  Message msg = MessageParser::Parse(kLf);
  ASSERT_TRUE(msg.subject.has_value());
  EXPECT_EQ(*msg.subject, "LF only");
}

}  // namespace
}  // namespace imap
