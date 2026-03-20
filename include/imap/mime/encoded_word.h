#ifndef IMAP_MIME_ENCODED_WORD_H_
#define IMAP_MIME_ENCODED_WORD_H_

#include <string>
#include <string_view>

namespace imap::mime {

/**
 * @brief Decodes a single RFC 2047 encoded-word token.
 *
 * Accepts tokens of the form @c =?charset?B?text?= (Base64) and
 * @c =?charset?Q?text?= (Quoted-Printable with @c _ as space).
 * Returns the original token unchanged on any parse or encoding failure.
 */
[[nodiscard]] std::string DecodeEncodedWord(std::string_view token);

/**
 * @brief Decodes all RFC 2047 encoded-words within a header value string.
 *
 * Whitespace between two consecutive encoded-words is discarded per
 * RFC 2047 §6.2. Plain text between encoded-words is preserved verbatim.
 */
[[nodiscard]] std::string DecodeHeaderValue(std::string_view header_value);

}  // namespace imap::mime

#endif  // IMAP_MIME_ENCODED_WORD_H_
