#ifndef IMAP_RESPONSE_H_
#define IMAP_RESPONSE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "imap/types.h"

namespace imap {

// ---------------------------------------------------------------------------
// Response code (optional bracketed code inside OK/NO/BAD, RFC 3501 §7.1)
// ---------------------------------------------------------------------------
enum class ResponseCode : uint8_t {
  kNone,
  kAlert,
  kAlreadyExists,
  kAppendUid,
  kAuthenticationFailed,
  kBadCharset,
  kCapability,
  kCopyUid,
  kExpungeIssued,
  kInUse,
  kLimit,
  kNonExistent,
  kNotExistent,
  kOverQuota,
  kParse,
  kPermanentFlags,
  kReadOnly,
  kReadWrite,
  kTryCreate,
  kUidNext,
  kUidValidity,
  kUnseen,
  kUnknown,
};

// Parsed [CODE …] block.
struct ResponseCodeData {
  ResponseCode code{ResponseCode::kNone};
  std::string raw_code;   // the token, e.g. "UIDNEXT"
  std::string arguments;  // everything after the code token inside []
};

// ---------------------------------------------------------------------------
// Capability response data
// ---------------------------------------------------------------------------
struct CapabilityData {
  std::vector<std::string> capabilities;
};

// ---------------------------------------------------------------------------
// EXISTS / RECENT / EXPUNGE value
// ---------------------------------------------------------------------------
struct CountData {
  uint32_t count{0};
};

// ---------------------------------------------------------------------------
// FLAGS response (mailbox flags list)
// ---------------------------------------------------------------------------
struct FlagsData {
  std::vector<std::string> flags;
};

// ---------------------------------------------------------------------------
// OK / NO / BAD / PREAUTH / BYE status response
// ---------------------------------------------------------------------------
struct StatusResponse {
  ResponseStatus status{ResponseStatus::kOk};
  ResponseCodeData code;
  std::string text;
};

// ---------------------------------------------------------------------------
// SELECT / EXAMINE data bundled per RFC 3501 §7.3
// ---------------------------------------------------------------------------
struct SelectData {
  uint32_t exists{0};
  uint32_t recent{0};
  std::optional<uint32_t> unseen;
  std::optional<uint32_t> uid_validity;
  std::optional<uint32_t> uid_next;
  std::vector<std::string> flags;
  std::vector<std::string> permanent_flags;
  bool read_only{false};
};

// ---------------------------------------------------------------------------
// SEARCH response — list of sequence numbers / UIDs
// ---------------------------------------------------------------------------
struct SearchData {
  std::vector<uint32_t> numbers;
};

// ---------------------------------------------------------------------------
// STATUS response
// ---------------------------------------------------------------------------
struct StatusData {
  std::string mailbox;
  std::unordered_map<std::string, uint32_t> items;
};

// ---------------------------------------------------------------------------
// LIST / LSUB response
// ---------------------------------------------------------------------------
struct ListData {
  std::vector<MailboxInfo> mailboxes;
};

// ---------------------------------------------------------------------------
// FETCH response data
// ---------------------------------------------------------------------------
struct FetchData {
  std::vector<FetchedMessage> messages;
};

// ---------------------------------------------------------------------------
// APPEND / COPY UID data (UIDPLUS, RFC 4315)
// ---------------------------------------------------------------------------
struct AppendUidData {
  uint32_t uid_validity{0};
  uint32_t assigned_uid{0};
};

struct CopyUidData {
  uint32_t uid_validity{0};
  std::string source_set;
  std::string destination_set;
};

// ---------------------------------------------------------------------------
// Tagged completion response
// ---------------------------------------------------------------------------
struct TaggedResponse {
  std::string tag;
  StatusResponse status;
};

// ---------------------------------------------------------------------------
// A single parsed server response line.
// ---------------------------------------------------------------------------
struct UntaggedResponse {
  // The numeric sequence number prefix, if any (e.g. "3" in "* 3 EXISTS").
  std::optional<uint32_t> number;

  // The keyword after the optional number (e.g. "EXISTS", "FETCH", "FLAGS").
  std::string keyword;

  // Decoded payload (one of the data types above, or empty for simple lines).
  std::variant<
      std::monostate,
      CapabilityData,
      FlagsData,
      CountData,
      SelectData,
      SearchData,
      StatusData,
      ListData,
      FetchData,
      AppendUidData,
      CopyUidData,
      StatusResponse  // BYE / PREAUTH
  > data;
};

// ---------------------------------------------------------------------------
// ContinuationResponse  ("+")
// ---------------------------------------------------------------------------
struct ContinuationResponse {
  std::string text;  // may encode a Base64 challenge for SASL
};

// ---------------------------------------------------------------------------
// Top-level parsed response (one server line / literal unit).
// ---------------------------------------------------------------------------
struct ParsedResponse {
  ResponseType type{ResponseType::kUntagged};
  std::variant<TaggedResponse, UntaggedResponse, ContinuationResponse> payload;

  // Convenience accessors.
  [[nodiscard]] bool IsTagged() const {
    return type == ResponseType::kTagged;
  }
  [[nodiscard]] bool IsUntagged() const {
    return type == ResponseType::kUntagged;
  }
  [[nodiscard]] bool IsContinuation() const {
    return type == ResponseType::kContinuation;
  }

  [[nodiscard]] const TaggedResponse& Tagged() const {
    return std::get<TaggedResponse>(payload);
  }
  [[nodiscard]] const UntaggedResponse& Untagged() const {
    return std::get<UntaggedResponse>(payload);
  }
  [[nodiscard]] const ContinuationResponse& Continuation() const {
    return std::get<ContinuationResponse>(payload);
  }
};

}  // namespace imap

#endif  // IMAP_RESPONSE_H_
