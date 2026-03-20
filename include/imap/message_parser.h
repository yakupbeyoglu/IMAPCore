#ifndef IMAP_MESSAGE_PARSER_H_
#define IMAP_MESSAGE_PARSER_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imap/message.h"
#include "imap/mime/codec.h"
#include "imap/mime/encoded_word.h"
#include "imap/mime/security.h"

namespace imap {

/**
 * @class MessageParser
 * @brief RFC 2822 message parser that orchestrates MIME body structure parsing.
 *
 * Owns RFC 2822 structural parsing: header unfolding, header/body split, and
 * multipart recursion. Low-level encoding/decoding and extension parsing
 * delegate to the @c imap::mime sub-namespace.
 *
 * All methods are static; the class is a stateless parser façade.
 */
class MessageParser {
 public:
  /**
   * @brief Parses a raw RFC 2822/MIME message byte string.
   * @param raw The complete message data (supports CRLF and bare LF).
   * @return Fully populated @c Message.
   */
  [[nodiscard]] static Message Parse(std::string_view raw);

  // ---------------------------------------------------------------------------
  // Forwarding façade — delegates to imap::mime:: free functions.
  //
  // Provides a single-header entry point for callers that prefer not to
  // include individual mime/ sub-headers.
  // ---------------------------------------------------------------------------

  /// @see mime::DecodeEncodedWord
  [[nodiscard]] static std::string DecodeEncodedWord(std::string_view token) {
    return mime::DecodeEncodedWord(token);
  }

  /// @see mime::DecodeHeaderValue
  [[nodiscard]] static std::string DecodeHeaderValue(std::string_view value) {
    return mime::DecodeHeaderValue(value);
  }

  /// @see mime::ParseContentDisposition
  [[nodiscard]] static ContentDisposition ParseContentDisposition(
      std::string_view value) {
    return mime::ParseContentDisposition(value);
  }

  /// @see mime::DetectSecurity
  [[nodiscard]] static SecurityInfo DetectSecurity(const MimePart& part) {
    return mime::DetectSecurity(part);
  }

 private:
  /// Splits a raw message into a header block and a body block.
  [[nodiscard]] static std::pair<std::string_view, std::string_view>
  SplitHeaderBody(std::string_view raw);

  /// Parses RFC 2822 headers from @p header_block into @p msg.
  static void ParseHeaders(std::string_view header_block, Message& msg);

  /// Parses a (possibly MIME multipart) body recursively.
  [[nodiscard]] static MimePart ParseBody(std::string_view body_raw,
                                          const std::string& content_type,
                                          const std::string& encoding);

  /// Splits a multipart body into child @c MimePart objects.
  [[nodiscard]] static std::vector<MimePart> ParseMultipart(
      std::string_view body, std::string_view boundary);

  /// Decodes a content-transfer-encoded body using @c imap::mime:: codecs.
  [[nodiscard]] static std::string Decode(std::string_view encoded,
                                          const std::string& encoding);
};

}  // namespace imap

#endif  // IMAP_MESSAGE_PARSER_H_
