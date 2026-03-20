#ifndef IMAP_TYPES_H_
#define IMAP_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace imap {

/**
 * @enum SessionState
 * @brief IMAP Connection / Session State (RFC 3501 §3)
 */
enum class SessionState : uint8_t {
  kNotAuthenticated,  // Before LOGIN / AUTHENTICATE
  kAuthenticated,     // Logged in, no mailbox selected
  kSelected,          // A mailbox is open
  kLogout,            // LOGOUT in progress / connection closing
};

/**
 * @enum ResponseStatus
 * @brief IMAP Response status tags (RFC 3501 §7.1)
 */
enum class ResponseStatus : uint8_t {
  kOk,
  kNo,
  kBad,
  kPreauth,
  kBye,
};

/**
 * @enum ResponseType
 * @brief IMAP Response types
 */
enum class ResponseType : uint8_t {
  kTagged,     // <tag> OK/NO/BAD …
  kUntagged,   // * <data>
  kContinuation,  // + <text>
};

/**
 * @enum MessageFlag
 * @brief Message flags (RFC 3501 §2.3.2)
 */
enum class MessageFlag : uint8_t {
  kSeen      = 0x01,
  kAnswered  = 0x02,
  kFlagged   = 0x04,
  kDeleted   = 0x08,
  kDraft     = 0x10,
  kRecent    = 0x20,
};

// Bitmask helper
using MessageFlags = uint8_t;

inline MessageFlags operator|(MessageFlag a, MessageFlag b) {
  return static_cast<MessageFlags>(a) | static_cast<MessageFlags>(b);
}

inline bool HasFlag(MessageFlags flags, MessageFlag flag) {
  return (flags & static_cast<MessageFlags>(flag)) != 0;
}

/**
 * @enum CommandType
 * @brief IMAP Commands (RFC 3501 §6)
 */
enum class CommandType : uint8_t {
  // Any state
  kCapability,
  kNoop,
  kLogout,
  // Not-authenticated state
  kStartTls,
  kAuthenticate,
  kLogin,
  // Authenticated state
  kSelect,
  kExamine,
  kCreate,
  kDelete,
  kRename,
  kSubscribe,
  kUnsubscribe,
  kList,
  kLsub,
  kStatus,
  kAppend,
  // Selected state
  kCheck,
  kClose,
  kExpunge,
  kSearch,
  kFetch,
  kStore,
  kCopy,
  kUid,
  kIdle,      // RFC 2177
};

/**
 * @enum FetchItem
 * @brief IMAP Fetch Data Items (RFC 3501 §6.4.5)
 */
enum class FetchItem : uint32_t {
  kAll            = 1 << 0,   /**< shorthand macro */
  kFast           = 1 << 1,   /**< shorthand macro */
  kFull           = 1 << 2,   /**< shorthand macro */
  kBody           = 1 << 3,
  kBodyStructure  = 1 << 4,
  kEnvelope       = 1 << 5,
  kFlags          = 1 << 6,
  kInternalDate   = 1 << 7,
  kRfc822         = 1 << 8,
  kRfc822Header   = 1 << 9,
  kRfc822Size     = 1 << 10,
  kRfc822Text     = 1 << 11,
  kUid            = 1 << 12,
};

/**
 * @brief Bitmask of FetchItem values.
 */
using FetchItems = uint32_t;

/**
 * @brief Bitwise OR operator for FetchItem.
 * @param a First FetchItem.
 * @param b Second FetchItem.
 * @return Bitwise OR of a and b as FetchItems.
 */
inline FetchItems operator|(FetchItem a, FetchItem b) {
  return static_cast<FetchItems>(a) | static_cast<FetchItems>(b);
}

/**
 * @enum StatusItem
 * @brief Status data items (RFC 3501 §6.3.10)
 */
enum class StatusItem : uint8_t {
  kMessages,
  kRecent,
  kUidNext,
  kUidValidity,
  kUnseen,
};

/**
 * @enum SearchKey
 * @brief Search criteria keys (RFC 3501 §6.4.4)
 */
enum class SearchKey : uint8_t {
  kAll,
  kAnswered,
  kBcc,
  kBefore,
  kBody,
  kCc,
  kDeleted,
  kDraft,
  kFlagged,
  kFrom,
  kHeader,
  kKeyword,
  kLarger,
  kNew,
  kNot,
  kOld,
  kOn,
  kOr,
  kRecent,
  kSeen,
  kSentBefore,
  kSentOn,
  kSentSince,
  kSince,
  kSmaller,
  kSubject,
  kText,
  kTo,
  kUid,
  kUnanswered,
  kUndeleted,
  kUndraft,
  kUnflagged,
  kUnkeyword,
  kUnseen,
};

// ---------------------------------------------------------------------------
// Capability names (RFC 3501 §7.2.1 + extensions)
// ---------------------------------------------------------------------------
inline constexpr std::string_view kCapImap4Rev1 = "IMAP4rev1";
inline constexpr std::string_view kCapImap4Rev2 = "IMAP4rev2";
inline constexpr std::string_view kCapStartTls  = "STARTTLS";
inline constexpr std::string_view kCapIdle      = "IDLE";
inline constexpr std::string_view kCapLiteral   = "LITERAL+";
inline constexpr std::string_view kCapSaslIr    = "SASL-IR";
inline constexpr std::string_view kCapAuthPlain = "AUTH=PLAIN";
inline constexpr std::string_view kCapAuthLogin = "AUTH=LOGIN";
inline constexpr std::string_view kCapUidPlus   = "UIDPLUS";

// ---------------------------------------------------------------------------
// Sequence-set (can be a single number, range, or comma list)
// ---------------------------------------------------------------------------
struct SequenceNumber {
  uint32_t value{0};
  bool is_wildcard{false};  // '*'

  static SequenceNumber Wildcard() { return {0, true}; }
  static SequenceNumber From(uint32_t v) { return {v, false}; }
};

struct SequenceRange {
  SequenceNumber start;
  SequenceNumber end;
};

using SequenceSet = std::vector<SequenceRange>;

// ---------------------------------------------------------------------------
// Mailbox status information (RFC 3501 §7.2.4)
// ---------------------------------------------------------------------------
struct MailboxStatus {
  std::string name;
  uint32_t messages{0};
  uint32_t recent{0};
  uint32_t uid_next{0};
  uint32_t uid_validity{0};
  uint32_t unseen{0};
};

// ---------------------------------------------------------------------------
// List / Lsub result entry (RFC 3501 §7.2.2 / §7.2.3)
// ---------------------------------------------------------------------------
struct MailboxInfo {
  std::vector<std::string> attributes;
  std::string hierarchy_delimiter;
  std::string name;
};

// ---------------------------------------------------------------------------
// Envelope structure (RFC 3501 §7.4.2)
// ---------------------------------------------------------------------------
struct AddressInfo {
  std::optional<std::string> display_name;
  std::optional<std::string> source_route;
  std::optional<std::string> mailbox;
  std::optional<std::string> host;
};

struct Envelope {
  std::optional<std::string> date;
  std::optional<std::string> subject;
  std::vector<AddressInfo> from;
  std::vector<AddressInfo> sender;
  std::vector<AddressInfo> reply_to;
  std::vector<AddressInfo> to;
  std::vector<AddressInfo> cc;
  std::vector<AddressInfo> bcc;
  std::optional<std::string> in_reply_to;
  std::optional<std::string> message_id;
};

// ---------------------------------------------------------------------------
// Body section / BODYSTRUCTURE (simplified)
// ---------------------------------------------------------------------------
struct BodySection {
  std::string section;   // e.g. "" (full), "TEXT", "HEADER", "1", "1.2"
  std::optional<uint64_t> offset;
  std::optional<uint64_t> size;
};

struct BodyStructurePart {
  std::string media_type;    // e.g. "TEXT"
  std::string subtype;       // e.g. "PLAIN"
  std::unordered_map<std::string, std::string> parameters;
  std::optional<std::string> content_id;
  std::optional<std::string> description;
  std::string encoding;      // e.g. "7BIT", "BASE64"
  uint64_t size{0};
  std::vector<BodyStructurePart> parts;  // multipart children
};

// ---------------------------------------------------------------------------
// A single fetched message attribute set
// ---------------------------------------------------------------------------
struct FetchedMessage {
  uint32_t sequence_number{0};
  std::optional<uint32_t> uid;
  MessageFlags flags{0};
  std::optional<std::string> internal_date;
  std::optional<uint64_t> rfc822_size;
  std::optional<std::string> rfc822_header;
  std::optional<std::string> rfc822_text;
  std::optional<std::string> rfc822;
  std::optional<Envelope> envelope;
  std::optional<BodyStructurePart> body_structure;
  std::unordered_map<std::string, std::string> body_sections;  // section → data
};

}  // namespace imap

#endif  // IMAP_TYPES_H_
