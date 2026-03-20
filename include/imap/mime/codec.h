#ifndef IMAP_MIME_CODEC_H_
#define IMAP_MIME_CODEC_H_

#include <string>
#include <string_view>

namespace imap::mime {

/**
 * @brief Decodes a Base64-encoded string (RFC 4648). Whitespace is ignored.
 */
[[nodiscard]] std::string DecodeBase64(std::string_view input);

/**
 * @brief Decodes a Quoted-Printable encoded string (RFC 2045 §6.7).
 */
[[nodiscard]] std::string DecodeQuotedPrintable(std::string_view input);

/**
 * @brief Converts a charset-encoded byte string to UTF-8.
 *
 * Supports UTF-8, US-ASCII, and ISO-8859-1 (Latin-1).
 * Returns the input bytes unchanged for unrecognised charsets.
 */
[[nodiscard]] std::string ToUtf8(std::string_view bytes, std::string_view charset);

/**
 * @brief Returns a lowercase copy of @p sv.
 */
[[nodiscard]] std::string ToLower(std::string_view sv);

/**
 * @brief Returns @p sv with leading and trailing ASCII whitespace stripped.
 */
[[nodiscard]] std::string_view Trim(std::string_view sv);

/**
 * @brief Extracts a named parameter from a structured header field value.
 *
 * Comparison is case-insensitive. Surrounding quotes are stripped from the
 * returned value.
 *
 * @code
 * ExtractParam("text/html; charset=utf-8", "charset") // -> "utf-8"
 * @endcode
 */
[[nodiscard]] std::string ExtractParam(std::string_view header_value,
                                       std::string_view param_name);

}  // namespace imap::mime

#endif  // IMAP_MIME_CODEC_H_
