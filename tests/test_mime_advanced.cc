#include "imap/message_parser.h"
#include "imap/mime/encoded_word.h"
#include "imap/mime/security.h"

#include <gtest/gtest.h>

#include <string>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// MessageParser instance used across tests
// ---------------------------------------------------------------------------

MessageParser& Parser() {
  static MessageParser p;
  return p;
}

// ===========================================================================
// RFC 2047 encoded-word decoding
// ===========================================================================

TEST(DecodeEncodedWordTest, PlainAsciiTokenReturnedAsIs) {
  EXPECT_EQ(Parser().DecodeEncodedWord("Hello"), "Hello");
}

TEST(DecodeEncodedWordTest, QuotedPrintableUtf8) {
  // =?UTF-8?Q?Hello_World?= → "Hello World"
  std::string token = "=?UTF-8?Q?Hello_World?=";
  EXPECT_EQ(Parser().DecodeEncodedWord(token), "Hello World");
}

TEST(DecodeEncodedWordTest, Base64Utf8) {
  // =?UTF-8?B?SGVsbG8gV29ybGQ=?= → "Hello World"
  std::string token = "=?UTF-8?B?SGVsbG8gV29ybGQ=?=";
  EXPECT_EQ(Parser().DecodeEncodedWord(token), "Hello World");
}

TEST(DecodeEncodedWordTest, QuotedPrintableLatin1) {
  // =?ISO-8859-1?Q?caf=E9?= → "café" in UTF-8
  std::string token = "=?ISO-8859-1?Q?caf=E9?=";
  std::string result = Parser().DecodeEncodedWord(token);
  // UTF-8 for 'é' (U+00E9) is 0xC3 0xA9 → "café"
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("caf"), std::string::npos);
}

TEST(DecodeEncodedWordTest, UnknownEncodingReturnedRaw) {
  std::string token = "=?UTF-8?X?foo?=";
  // Unknown encoding 'X' → return token unchanged.
  EXPECT_EQ(Parser().DecodeEncodedWord(token), token);
}

TEST(DecodeEncodedWordTest, TooShortTokenReturnedRaw) {
  EXPECT_EQ(Parser().DecodeEncodedWord("=?x?="), "=?x?=");
}

TEST(DecodeEncodedWordTest, MissingClosingReturnsRaw) {
  EXPECT_EQ(Parser().DecodeEncodedWord("=?UTF-8?Q?foo"), "=?UTF-8?Q?foo");
}

// ---------------------------------------------------------------------------
// DecodeHeaderValue — multiple encoded-words and plain text
// ---------------------------------------------------------------------------

TEST(DecodeHeaderValueTest, NoEncodedWordsPassThrough) {
  EXPECT_EQ(Parser().DecodeHeaderValue("Hello World"), "Hello World");
}

TEST(DecodeHeaderValueTest, SingleEncodedWord) {
  // "Subject: =?UTF-8?B?SGVsbG8=?=" → "Hello"
  std::string result =
      Parser().DecodeHeaderValue("=?UTF-8?B?SGVsbG8=?=");
  EXPECT_EQ(result, "Hello");
}

TEST(DecodeHeaderValueTest, MixedPlainAndEncoded) {
  // "Re: =?UTF-8?Q?Hello?=" → "Re: Hello"
  std::string result =
      Parser().DecodeHeaderValue("Re: =?UTF-8?Q?Hello?=");
  EXPECT_EQ(result, "Re: Hello");
}

TEST(DecodeHeaderValueTest, ConsecutiveEncodedWordsNoSpaceBetween) {
  // RFC 2047: whitespace between two encoded-words is discarded.
  std::string val =
      "=?UTF-8?Q?Hello?= =?UTF-8?Q?_World?=";
  std::string result = Parser().DecodeHeaderValue(val);
  // Should give "Hello World" (the _ in Q becomes space, whitespace discarded)
  EXPECT_NE(result.find("Hello"), std::string::npos);
  EXPECT_NE(result.find("World"), std::string::npos);
}

// ===========================================================================
// ParseContentDisposition
// ===========================================================================

TEST(ParseContentDispositionTest, AttachmentType) {
  ContentDisposition cd =
      Parser().ParseContentDisposition("attachment; filename=\"test.pdf\"");
  EXPECT_EQ(cd.type, ContentDispositionType::kAttachment);
  EXPECT_EQ(cd.filename.value_or(""), "test.pdf");
}

TEST(ParseContentDispositionTest, InlineType) {
  ContentDisposition cd = Parser().ParseContentDisposition("inline");
  EXPECT_EQ(cd.type, ContentDispositionType::kInline);
}

TEST(ParseContentDispositionTest, NoneTypeOnEmpty) {
  ContentDisposition cd = Parser().ParseContentDisposition("");
  EXPECT_EQ(cd.type, ContentDispositionType::kNone);
}

TEST(ParseContentDispositionTest, EncodedFilename) {
  // filename is an RFC 2047 encoded-word
  ContentDisposition cd = Parser().ParseContentDisposition(
      "attachment; filename=\"=?UTF-8?B?dGVzdC5wZGY=?=\"");
  // =?UTF-8?B?dGVzdC5wZGY=?= → "test.pdf"
  EXPECT_EQ(cd.type, ContentDispositionType::kAttachment);
  EXPECT_EQ(cd.filename.value_or(""), "test.pdf");
}

TEST(ParseContentDispositionTest, SizeParameter) {
  ContentDisposition cd = Parser().ParseContentDisposition(
      "attachment; filename=\"doc.txt\"; size=1024");
  EXPECT_EQ(cd.size.value_or(0), 1024u);
}

TEST(ParseContentDispositionTest, NoFilenameIsEmpty) {
  ContentDisposition cd = Parser().ParseContentDisposition("attachment");
  EXPECT_FALSE(cd.filename.has_value());
}

// ===========================================================================
// DetectSecurity
// ===========================================================================

TEST(DetectSecurityTest, PlainPartIsNone) {
  MimePart part;
  part.content_type  = "text/plain";
  part.decoded_body  = "Just some text";
  SecurityInfo info  = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kNone);
}

TEST(DetectSecurityTest, PgpInlineSigned) {
  MimePart part;
  part.content_type  = "text/plain";
  part.decoded_body  =
      "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA256\n\nHello\n"
      "-----BEGIN PGP SIGNATURE-----\n...\n-----END PGP SIGNATURE-----";
  SecurityInfo info  = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kPgpInlineSigned);
}

TEST(DetectSecurityTest, PgpInlineEncrypted) {
  MimePart part;
  part.content_type  = "text/plain";
  part.decoded_body  = "-----BEGIN PGP MESSAGE-----\n...\n-----END PGP MESSAGE-----";
  SecurityInfo info  = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kPgpInlineEncrypted);
}

TEST(DetectSecurityTest, PgpMimeSigned) {
  // Simulate a multipart/signed part with PGP protocol
  MimePart part;
  part.content_type = "multipart/signed";
  part.headers["content-type"] =
      "multipart/signed; protocol=\"application/pgp-signature\"; "
      "micalg=pgp-sha256; boundary=\"sig-boundary\"";

  MimePart body_part;
  body_part.content_type = "text/plain";
  body_part.body = "Signed content";

  MimePart sig_part;
  sig_part.content_type = "application/pgp-signature";
  sig_part.body = "--- signature ---";

  part.parts.push_back(body_part);
  part.parts.push_back(sig_part);

  SecurityInfo info = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kPgpSigned);
  EXPECT_EQ(info.signed_data, "Signed content");
  EXPECT_EQ(info.signature, "--- signature ---");
}

TEST(DetectSecurityTest, SmimeSigned) {
  MimePart part;
  part.content_type = "multipart/signed";
  part.headers["content-type"] =
      "multipart/signed; protocol=\"application/pkcs7-signature\"; "
      "boundary=\"smime-boundary\"";

  MimePart body_part;
  body_part.body = "SMIME signed content";

  MimePart sig_part;
  sig_part.body = "--- smime sig ---";

  part.parts.push_back(body_part);
  part.parts.push_back(sig_part);

  SecurityInfo info = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kSmimeSigned);
}

TEST(DetectSecurityTest, SmimeMime) {
  MimePart part;
  part.content_type = "application/pkcs7-mime";
  part.body = "opaque-smime-blob";
  SecurityInfo info = Parser().DetectSecurity(part);
  EXPECT_EQ(info.type, SecurityType::kSmimeEncrypted);
}

// ===========================================================================
// MimePart helpers: Attachments, PlainTextPart, HtmlPart
// ===========================================================================

TEST(MimePartHelperTest, LeafPlainTextIsNotAttachment) {
  MimePart part;
  part.content_type        = "text/plain";
  part.disposition.type    = ContentDispositionType::kNone;
  EXPECT_FALSE(part.IsAttachment());
  EXPECT_TRUE(part.IsInline());
}

TEST(MimePartHelperTest, AttachmentFlaggedCorrectly) {
  MimePart part;
  part.content_type        = "application/pdf";
  part.disposition.type    = ContentDispositionType::kAttachment;
  part.disposition.filename = "report.pdf";
  EXPECT_TRUE(part.IsAttachment());
}

TEST(MimePartHelperTest, AttachmentsCollectsLeaves) {
  // Build  multipart/mixed
  //   ├── text/plain   (inline)
  //   ├── application/pdf (attachment)
  //   └── image/jpeg   (attachment)
  MimePart root;
  root.content_type = "multipart/mixed";

  MimePart text;
  text.content_type      = "text/plain";
  text.disposition.type  = ContentDispositionType::kNone;

  MimePart pdf;
  pdf.content_type      = "application/pdf";
  pdf.disposition.type  = ContentDispositionType::kAttachment;
  pdf.disposition.filename = "doc.pdf";

  MimePart img;
  img.content_type      = "image/jpeg";
  img.disposition.type  = ContentDispositionType::kAttachment;
  img.disposition.filename = "photo.jpg";

  root.parts.push_back(text);
  root.parts.push_back(pdf);
  root.parts.push_back(img);

  auto attachments = root.Attachments();
  ASSERT_EQ(attachments.size(), 2u);
}

TEST(MimePartHelperTest, PlainTextPartFound) {
  MimePart root;
  root.content_type = "multipart/alternative";

  MimePart text;
  text.content_type = "text/plain";
  text.decoded_body = "Plain text body";

  MimePart html;
  html.content_type = "text/html";
  html.decoded_body = "<b>HTML body</b>";

  root.parts.push_back(text);
  root.parts.push_back(html);

  const MimePart* plain = root.PlainTextPart();
  ASSERT_NE(plain, nullptr);
  EXPECT_EQ(plain->decoded_body, "Plain text body");
}

TEST(MimePartHelperTest, HtmlPartFound) {
  MimePart root;
  root.content_type = "multipart/alternative";

  MimePart text;
  text.content_type = "text/plain";

  MimePart html;
  html.content_type = "text/html";
  html.decoded_body = "<b>HTML</b>";

  root.parts.push_back(text);
  root.parts.push_back(html);

  const MimePart* hp = root.HtmlPart();
  ASSERT_NE(hp, nullptr);
  EXPECT_EQ(hp->decoded_body, "<b>HTML</b>");
}

TEST(MimePartHelperTest, PlainTextPartReturnsNullWhenAbsent) {
  MimePart root;
  root.content_type = "multipart/alternative";

  MimePart html;
  html.content_type = "text/html";
  root.parts.push_back(html);

  EXPECT_EQ(root.PlainTextPart(), nullptr);
}

TEST(MimePartHelperTest, HtmlPartReturnsNullWhenAbsent) {
  MimePart root;
  root.content_type = "multipart/alternative";

  MimePart text;
  text.content_type = "text/plain";
  root.parts.push_back(text);

  EXPECT_EQ(root.HtmlPart(), nullptr);
}

// ===========================================================================
// Full message parse with Content-Disposition and Content-ID
// ===========================================================================

TEST(MessageParserIntegrationTest, MultipartMixedWithAttachment) {
  static constexpr std::string_view kRaw =
      "MIME-Version: 1.0\r\n"
      "Content-Type: multipart/mixed; boundary=\"----=_Part_1\"\r\n"
      "Subject: =?UTF-8?B?VGVzdCBTdWJqZWN0?=\r\n"
      "\r\n"
      "------=_Part_1\r\n"
      "Content-Type: text/plain; charset=UTF-8\r\n"
      "Content-Transfer-Encoding: 7bit\r\n"
      "\r\n"
      "Hello, World!\r\n"
      "------=_Part_1\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Transfer-Encoding: base64\r\n"
      "Content-Disposition: attachment; filename=\"test.bin\"\r\n"
      "Content-ID: <part001@example.com>\r\n"
      "\r\n"
      "SGVsbG8=\r\n"
      "------=_Part_1--\r\n";

  MessageParser parser;
  Message msg = parser.Parse(kRaw);

  // Subject was Base64-encoded.
  ASSERT_TRUE(msg.subject.has_value());
  EXPECT_EQ(msg.subject.value(), "Test Subject");

  // Body should be multipart.
  EXPECT_EQ(msg.body.content_type, "multipart/mixed");
  ASSERT_GE(msg.body.parts.size(), 2u);

  // First part: text/plain.
  const MimePart& plain = msg.body.parts[0];
  EXPECT_EQ(plain.content_type, "text/plain");

  // Second part: attachment.
  const MimePart& att = msg.body.parts[1];
  EXPECT_TRUE(att.IsAttachment());
  EXPECT_EQ(att.disposition.filename.value_or(""), "test.bin");
  EXPECT_TRUE(att.content_id.has_value());
  EXPECT_NE(att.content_id.value().find("part001"), std::string::npos);

  // Attachments() helper.
  auto attachments = msg.body.Attachments();
  EXPECT_EQ(attachments.size(), 1u);
}

}  // namespace
}  // namespace imap
