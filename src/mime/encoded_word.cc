#include "imap/mime/encoded_word.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "imap/mime/codec.h"

namespace imap::mime {

std::string DecodeEncodedWord(std::string_view token) {
  // Minimum valid token is "=?a?b?c?=" = 9 chars.
  if (token.size() < 9) return std::string(token);
  if (token.substr(0, 2) != "=?" || token.substr(token.size() - 2) != "?=")
    return std::string(token);

  std::string_view inner = token.substr(2, token.size() - 4);

  // charset?encoding?text
  auto q1 = inner.find('?');
  if (q1 == std::string_view::npos) return std::string(token);
  std::string charset = ToLower(inner.substr(0, q1));

  auto q2 = inner.find('?', q1 + 1);
  if (q2 == std::string_view::npos) return std::string(token);
  std::string enc = ToLower(inner.substr(q1 + 1, q2 - q1 - 1));

  std::string_view encoded_text = inner.substr(q2 + 1);

  std::string decoded;
  if (enc == "b") {
    decoded = DecodeBase64(encoded_text);
  } else if (enc == "q") {
    // RFC 2047 Q encoding: underscore represents 0x20 (space).
    std::string qp_text(encoded_text);
    std::replace(qp_text.begin(), qp_text.end(), '_', ' ');
    decoded = DecodeQuotedPrintable(qp_text);
  } else {
    return std::string(token);  // Unknown encoding — return raw token.
  }

  return ToUtf8(decoded, charset);
}

std::string DecodeHeaderValue(std::string_view header_value) {
  std::string result;
  result.reserve(header_value.size());

  std::size_t pos = 0;
  while (pos < header_value.size()) {
    auto start = header_value.find("=?", pos);
    if (start == std::string_view::npos) {
      result += header_value.substr(pos);
      break;
    }

    result += header_value.substr(pos, start - pos);

    auto end = header_value.find("?=", start + 2);
    if (end == std::string_view::npos) {
      result += header_value.substr(start);
      break;
    }
    end += 2;  // include the closing '?='

    result += DecodeEncodedWord(header_value.substr(start, end - start));
    pos = end;

    // RFC 2047 §6.2: whitespace between consecutive encoded-words is ignored.
    while (pos < header_value.size() &&
           (header_value[pos] == ' ' || header_value[pos] == '\t')) {
      if (pos + 1 < header_value.size() &&
          header_value.substr(pos + 1, 2) == "=?") {
        ++pos;
      } else {
        break;
      }
    }
  }

  return result;
}

}  // namespace imap::mime
