#ifndef IMAP_MIME_SECURITY_H_
#define IMAP_MIME_SECURITY_H_

#include <string_view>

#include "imap/message.h"

namespace imap::mime {

/**
 * @brief Parses a raw Content-Disposition header value (RFC 2183).
 *
 * Decodes RFC 2047-encoded filenames. Strips surrounding quotes from parameter
 * values. Returns a default-constructed ContentDisposition (kNone) on empty
 * input.
 */
[[nodiscard]] ContentDisposition ParseContentDisposition(
    std::string_view header_value);

/**
 * @brief Detects S/MIME or PGP security wrappers in a MIME part (RFC 3156 / RFC 5751).
 *
 * Inspects @p part.content_type and, for multipart types, the @c protocol
 * parameter from its Content-Type header. Also detects PGP inline-armoured
 * messages via the decoded body text.
 *
 * @return Populated SecurityInfo. @c type == SecurityType::kNone when no
 *         security wrapper is found.
 */
[[nodiscard]] SecurityInfo DetectSecurity(const MimePart& part);

}  // namespace imap::mime

#endif  // IMAP_MIME_SECURITY_H_
