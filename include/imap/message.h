#ifndef IMAP_MESSAGE_H_
#define IMAP_MESSAGE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace imap {

/**
 * @brief RFC 2822 / MIME message representation
 */

// ---------------------------------------------------------------------------
// Content-Disposition (RFC 2183)
// ---------------------------------------------------------------------------

/**
 * @enum ContentDispositionType
 * @brief Disposition type for a MIME body part.
 */
enum class ContentDispositionType : uint8_t {
  kNone,
  kInline,
  kAttachment,
};

/**
 * @struct ContentDisposition
 * @brief Parsed Content-Disposition header (RFC 2183).
 */
struct ContentDisposition {
  ContentDispositionType type{ContentDispositionType::kNone};
  std::optional<std::string> filename;    ///< filename parameter (decoded)
  std::optional<std::string> creation_date;
  std::optional<std::string> modification_date;
  std::optional<std::string> read_date;
  std::optional<uint64_t>    size;        ///< size parameter in bytes
};

// ---------------------------------------------------------------------------
// Security layer type (S/MIME / PGP)
// ---------------------------------------------------------------------------

/**
 * @enum SecurityType
 * @brief Type of security wrapper applied to the message or part.
 */
enum class SecurityType : uint8_t {
  kNone,
  kSmimeSigned,          ///< S/MIME multipart/signed
  kSmimeEncrypted,       ///< S/MIME application/pkcs7-mime envelopedData
  kPgpSigned,            ///< PGP/MIME multipart/signed (RFC 3156)
  kPgpEncrypted,         ///< PGP/MIME multipart/encrypted (RFC 3156)
  kPgpInlineSigned,      ///< PGP inline -----BEGIN PGP SIGNED MESSAGE-----
  kPgpInlineEncrypted,   ///< PGP inline -----BEGIN PGP MESSAGE-----
};

/**
 * @struct SecurityInfo
 * @brief Security metadata associated with a message or MIME part.
 */
struct SecurityInfo {
  SecurityType type{SecurityType::kNone};
  std::string protocol;     ///< e.g. "application/pgp-signature"
  std::string micalg;       ///< e.g. "pgp-sha256"
  std::string signed_data;  ///< raw signed content (if signed)
  std::string signature;    ///< raw signature blob
};

// ---------------------------------------------------------------------------
// MimePart — one MIME body part (RFC 2045-2049)
// ---------------------------------------------------------------------------

/**
 * @struct MimePart
 * @brief A single MIME body part.
 */
struct MimePart {
  std::unordered_map<std::string, std::string> headers;
  std::string content_type;        ///< e.g. "text/plain"
  std::string charset;             ///< e.g. "utf-8"
  std::string encoding;            ///< e.g. "base64", "quoted-printable", "7bit"
  std::string body;                ///< raw (possibly encoded) body
  std::string decoded_body;        ///< decoded content (UTF-8 string)

  // Sub-parts (multipart/*)
  std::vector<MimePart> parts;
  std::string boundary;            ///< multipart boundary parameter

  // RFC 2183 Content-Disposition
  ContentDisposition disposition;

  // Content-ID header (used for CID: references in HTML mail)
  std::optional<std::string> content_id;

  // Description (Content-Description header)
  std::optional<std::string> description;

  // Security layer (S/MIME / PGP)
  SecurityInfo security;

  // ---------------------------------------------------------------------------
  // Convenience helpers
  // ---------------------------------------------------------------------------

  /**
   * @brief Returns true if this part is an attachment.
   */
  [[nodiscard]] bool IsAttachment() const noexcept {
    return disposition.type == ContentDispositionType::kAttachment;
  }

  /**
   * @brief Returns true if this part is an inline part (text/plain, text/html, inline image).
   */
  [[nodiscard]] bool IsInline() const noexcept {
    return disposition.type == ContentDispositionType::kInline ||
           disposition.type == ContentDispositionType::kNone;
  }

  /**
   * @brief Collects all leaf attachment parts recursively.
   * @return Vector of const-pointers to attachment MimeParts.
   */
  [[nodiscard]] std::vector<const MimePart*> Attachments() const;

  /**
   * @brief Returns the preferred text/plain body (searches subtree).
   * @return Pointer to the first text/plain part, or nullptr.
   */
  [[nodiscard]] const MimePart* PlainTextPart() const;

  /**
   * @brief Returns the preferred text/html body (searches subtree).
   * @return Pointer to the first text/html part, or nullptr.
   */
  [[nodiscard]] const MimePart* HtmlPart() const;
};

// ---------------------------------------------------------------------------
// Message — full RFC 2822 message
// ---------------------------------------------------------------------------

/**
 * @struct Message
 * @brief Full parsed RFC 2822 message.
 */
struct Message {
  // --- RFC 2822 headers (lower-cased field names, decoded) ---
  std::unordered_map<std::string, std::string> headers;

  // Convenience accessors for decoded common headers (RFC 2047 applied).
  std::optional<std::string> date;
  std::optional<std::string> subject;      ///< RFC 2047-decoded subject
  std::optional<std::string> from;
  std::optional<std::string> to;
  std::optional<std::string> cc;
  std::optional<std::string> bcc;
  std::optional<std::string> reply_to;
  std::optional<std::string> message_id;
  std::optional<std::string> in_reply_to;
  std::optional<std::string> references;

  // MIME body tree.
  MimePart body;

  // Message-level security info (top-level multipart/signed or encrypted).
  SecurityInfo security;

  // Raw bytes of the full message (as ingested).
  std::string raw;

  // IMAP sequence / UID assigned during fetch.
  uint32_t sequence_number{0};
  std::optional<uint32_t> uid;
};



}  // namespace imap

#endif  // IMAP_MESSAGE_H_
