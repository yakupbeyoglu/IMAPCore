#include "imap/message_parser.h"

#ifdef DEBUG
#include <spdlog/spdlog.h>
#endif

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imap/mime/codec.h"
#include "imap/mime/encoded_word.h"
#include "imap/mime/security.h"

namespace imap {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Message MessageParser::Parse(std::string_view raw) {
#ifdef DEBUG
  spdlog::debug("MessageParser::Parse() input size: {}", raw.size());
#endif

  Message msg;
  msg.raw = std::string(raw);

  auto [header_block, body_block] = SplitHeaderBody(raw);

#ifdef DEBUG
  spdlog::debug("MessageParser::Parse() header: {} bytes, body: {} bytes",
                header_block.size(), body_block.size());
#endif

  ParseHeaders(header_block, msg);

  std::string ct  = "text/plain";
  std::string enc = "7bit";

  if (auto it = msg.headers.find("content-type"); it != msg.headers.end())
    ct = it->second;
  if (auto it = msg.headers.find("content-transfer-encoding");
      it != msg.headers.end())
    enc = it->second;

  msg.body = ParseBody(body_block, ct, enc);
  return msg;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::pair<std::string_view, std::string_view>
MessageParser::SplitHeaderBody(std::string_view raw) {
  // RFC 2822: blank line separates headers from body (CRLF or bare LF).
  if (auto crlf = raw.find("\r\n\r\n"); crlf != std::string_view::npos)
    return {raw.substr(0, crlf + 2), raw.substr(crlf + 4)};
  if (auto lf = raw.find("\n\n"); lf != std::string_view::npos)
    return {raw.substr(0, lf + 1), raw.substr(lf + 2)};
  return {raw, {}};  // header-only message
}

void MessageParser::ParseHeaders(std::string_view header_block, Message& msg) {
#ifdef DEBUG
  spdlog::debug("MessageParser::ParseHeaders() block size: {}",
                header_block.size());
#endif

  // RFC 2822 §2.2.3: unfold header continuation lines.
  std::string unfolded;
  unfolded.reserve(header_block.size());
  for (std::size_t i = 0; i < header_block.size(); ) {
    char c = header_block[i];
    if ((c == '\r' || c == '\n') && i + 1 < header_block.size()) {
      if (c == '\r' && header_block[i + 1] == '\n') i += 2;
      else ++i;
      if (i < header_block.size() &&
          (header_block[i] == ' ' || header_block[i] == '\t')) {
        unfolded += ' ';  // fold → single space
        continue;
      }
      unfolded += '\n';
    } else {
      unfolded += c;
      ++i;
    }
  }

  // Parse "Field: value\n" lines.
  std::size_t pos = 0;
  while (pos < unfolded.size()) {
    std::size_t nl = unfolded.find('\n', pos);
    std::string_view line =
        (nl == std::string::npos)
            ? std::string_view(unfolded).substr(pos)
            : std::string_view(unfolded).substr(pos, nl - pos);
    pos = (nl == std::string::npos) ? unfolded.size() : nl + 1;

    if (line.empty()) continue;

    auto colon = line.find(':');
    if (colon == std::string_view::npos) continue;

    std::string name  = mime::ToLower(line.substr(0, colon));
    std::string value{mime::Trim(line.substr(colon + 1))};
    msg.headers[name] = value;
  }

  // Fill convenience fields; apply RFC 2047 decoding to user-visible headers.
  auto get = [&](const char* k) -> std::optional<std::string> {
    auto it = msg.headers.find(k);
    if (it != msg.headers.end()) return it->second;
    return std::nullopt;
  };
  auto getDecoded = [&](const char* k) -> std::optional<std::string> {
    auto it = msg.headers.find(k);
    if (it == msg.headers.end()) return std::nullopt;
    return mime::DecodeHeaderValue(it->second);
  };

  msg.date        = get("date");
  msg.subject     = getDecoded("subject");
  msg.from        = getDecoded("from");
  msg.to          = getDecoded("to");
  msg.cc          = getDecoded("cc");
  msg.bcc         = getDecoded("bcc");
  msg.reply_to    = getDecoded("reply-to");
  msg.message_id  = get("message-id");
  msg.in_reply_to = get("in-reply-to");
  msg.references  = get("references");
}

MimePart MessageParser::ParseBody(std::string_view body_raw,
                                   const std::string& content_type,
                                   const std::string& encoding) {
#ifdef DEBUG
  spdlog::debug("MessageParser::ParseBody() ct: {}, enc: {}",
                content_type, encoding);
#endif

  MimePart part;
  part.content_type = mime::ToLower(
      content_type.substr(0, content_type.find(';')));
  std::string_view ct_sv = content_type;
  part.charset  = mime::ToLower(mime::ExtractParam(ct_sv, "charset"));
  part.encoding = mime::ToLower(encoding.substr(0, encoding.find(';')));

  while (!part.encoding.empty() &&
         std::isspace(static_cast<unsigned char>(part.encoding.back())))
    part.encoding.pop_back();

  if (part.content_type.find("multipart/") == 0) {
    std::string boundary = mime::ExtractParam(ct_sv, "boundary");
    part.boundary = boundary;
#ifdef DEBUG
    spdlog::debug("MessageParser::ParseBody() multipart boundary: {}", boundary);
#endif
    part.parts    = ParseMultipart(body_raw, boundary);
    part.security = mime::DetectSecurity(part);
  } else {
    part.body         = std::string(body_raw);
    part.decoded_body = Decode(body_raw, part.encoding);
    part.security     = mime::DetectSecurity(part);
  }
  return part;
}

std::vector<MimePart> MessageParser::ParseMultipart(std::string_view body,
                                                     std::string_view boundary) {
#ifdef DEBUG
  spdlog::debug("MessageParser::ParseMultipart() boundary: {}", boundary);
#endif

  std::vector<MimePart> parts;
  if (boundary.empty()) return parts;

  std::string delimiter = "--";
  delimiter += boundary;

  std::size_t pos = 0;
  // Skip preamble before the first delimiter.
  {
    auto d = body.find(delimiter, pos);
    if (d == std::string_view::npos) return parts;
    pos = d + delimiter.size();
    if (pos < body.size() && body[pos] == '\r') ++pos;
    if (pos < body.size() && body[pos] == '\n') ++pos;
  }

  while (pos < body.size()) {
    auto next_delim = body.find(delimiter, pos);
    if (next_delim == std::string_view::npos) break;

    std::string_view part_raw = body.substr(pos, next_delim - pos);
    // Strip trailing CRLF before the delimiter line.
    while (!part_raw.empty() &&
           (part_raw.back() == '\n' || part_raw.back() == '\r'))
      part_raw.remove_suffix(1);

    auto [hdr, bdy] = SplitHeaderBody(part_raw);
    Message sub_msg;
    ParseHeaders(hdr, sub_msg);

    std::string ct  = "text/plain";
    std::string enc = "7bit";
    if (auto it = sub_msg.headers.find("content-type");
        it != sub_msg.headers.end()) ct = it->second;
    if (auto it = sub_msg.headers.find("content-transfer-encoding");
        it != sub_msg.headers.end()) enc = it->second;

    MimePart sub_part = ParseBody(bdy, ct, enc);

    // Populate Content-ID (strip angle brackets).
    if (auto it = sub_msg.headers.find("content-id");
        it != sub_msg.headers.end()) {
      std::string cid = it->second;
      if (!cid.empty() && cid.front() == '<') cid = cid.substr(1);
      if (!cid.empty() && cid.back()  == '>') cid.pop_back();
      sub_part.content_id = cid;
    }

    // Populate Content-Description (RFC 2047 decoded).
    if (auto it = sub_msg.headers.find("content-description");
        it != sub_msg.headers.end())
      sub_part.description = mime::DecodeHeaderValue(it->second);

    // Populate Content-Disposition.
    if (auto it = sub_msg.headers.find("content-disposition");
        it != sub_msg.headers.end())
      sub_part.disposition = mime::ParseContentDisposition(it->second);

    // Move sub-part headers in for callers that inspect them.
    sub_part.headers = std::move(sub_msg.headers);
    parts.push_back(std::move(sub_part));

    pos = next_delim + delimiter.size();
    if (pos + 2 <= body.size() && body.substr(pos, 2) == "--") break;
    if (pos < body.size() && body[pos] == '\r') ++pos;
    if (pos < body.size() && body[pos] == '\n') ++pos;
  }

  return parts;
}

std::string MessageParser::Decode(std::string_view encoded,
                                   const std::string& encoding) {
#ifdef DEBUG
  spdlog::debug("MessageParser::Decode() encoding: {}", encoding);
#endif
  if (encoding == "base64")           return mime::DecodeBase64(encoded);
  if (encoding == "quoted-printable") return mime::DecodeQuotedPrintable(encoded);
  return std::string(encoded);  // 7bit / 8bit / binary
}

}  // namespace imap
