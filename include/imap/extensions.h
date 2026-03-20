#ifndef IMAP_EXTENSIONS_H_
#define IMAP_EXTENSIONS_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imap/types.h"

namespace imap {

// ---------------------------------------------------------------------------
// Extension capability constants
// ---------------------------------------------------------------------------
inline constexpr std::string_view kCapXList       = "XLIST";       // GMail XLIST
inline constexpr std::string_view kCapMove        = "MOVE";        // RFC 6851
inline constexpr std::string_view kCapCondStore   = "CONDSTORE";   // RFC 7162
inline constexpr std::string_view kCapQResync     = "QRESYNC";     // RFC 7162
inline constexpr std::string_view kCapESearch     = "ESEARCH";     // RFC 4731
inline constexpr std::string_view kCapSortThread  = "SORT";        // RFC 5256
inline constexpr std::string_view kCapCompress    = "COMPRESS=DEFLATE"; // RFC 4978
inline constexpr std::string_view kCapSpecialUse  = "SPECIAL-USE"; // RFC 6154
inline constexpr std::string_view kCapObjectId    = "OBJECTID";    // RFC 8474
inline constexpr std::string_view kCapUnselect    = "UNSELECT";    // RFC 3691

// ---------------------------------------------------------------------------
// CONDSTORE / QRESYNC types (RFC 7162)
// ---------------------------------------------------------------------------

/**
 * @brief Represents a mod-sequence value (CONDSTORE / QRESYNC).
 */
using ModSequence = uint64_t;

/**
 * @struct VanishedData
 * @brief Data associated with a VANISHED untagged response (QRESYNC).
 */
struct VanishedData {
  bool earlier{false};   ///< VANISHED (EARLIER) — historical
  std::string uid_set;   ///< Space-formatted UID set that vanished
};

/**
 * @struct FetchModSeqData
 * @brief Extended FETCH data including MODSEQ (CONDSTORE).
 */
struct FetchModSeqData {
  uint32_t sequence_number{0};
  std::optional<uint32_t> uid;
  ModSequence mod_seq{0};
};

/**
 * @struct QResyncParams
 * @brief Parameters for SELECT QRESYNC (RFC 7162 §3.2.5).
 */
struct QResyncParams {
  uint32_t uid_validity{0};
  ModSequence last_mod_seq{0};
  std::optional<std::string> known_uid_set;     ///< optional UID set client knows about
  std::optional<std::string> seq_match_data;    ///< optional sequence/UID correspondence list
};

// ---------------------------------------------------------------------------
// SORT result (RFC 5256)
// ---------------------------------------------------------------------------
struct SortData {
  std::vector<uint32_t> numbers;  ///< Sorted sequence numbers / UIDs
};

// ---------------------------------------------------------------------------
// ESEARCH result (RFC 4731)
// ---------------------------------------------------------------------------
struct ESearchData {
  std::optional<std::string> tag;        ///< Correlation tag
  bool uid{false};                       ///< Response contains UIDs
  std::optional<uint32_t> min_num;
  std::optional<uint32_t> max_num;
  std::optional<uint32_t> count;
  std::optional<std::string> all_set;   ///< ALL result set (sequence-set string)
  ModSequence mod_seq{0};               ///< MODSEQ value (if present)
};

// ---------------------------------------------------------------------------
// SPECIAL-USE mailbox attributes (RFC 6154)
// ---------------------------------------------------------------------------
enum class SpecialUseFlag : uint8_t {
  kNone       = 0,
  kAll        = 1 << 0,
  kArchive    = 1 << 1,
  kDrafts     = 1 << 2,
  kFlagged    = 1 << 3,
  kJunk       = 1 << 4,
  kSent       = 1 << 5,
  kTrash      = 1 << 6,
};
using SpecialUseFlags = uint8_t;

/**
 * @brief Parse a LIST attribute string into a SpecialUseFlags bitmask.
 * @param attribute Attribute string such as "\\Sent" or "\\Drafts".
 * @return Corresponding SpecialUseFlags value.
 */
SpecialUseFlags SpecialUseFlagFromString(std::string_view attribute);

// ---------------------------------------------------------------------------
// ExtensionCommandBuilder
// ---------------------------------------------------------------------------
/**
 * @class ExtensionCommandBuilder
 * @brief Builds IMAP extension command strings for XLIST, MOVE, CONDSTORE, QRESYNC, SORT, ESEARCH.
 *
 * Works alongside CommandBuilder. Each method returns a fully formatted
 * command string (with tag) ready to write to the transport.
 *
 * @note The caller is responsible for checking HasCapability() before using
 * extension commands.
 */
class ExtensionCommandBuilder {
 public:
  explicit ExtensionCommandBuilder(std::string_view tag_prefix = "A");

  /**
   * @brief Resets the tag counter.
   */
  void Reset();

  /**
   * @brief Returns the last generated tag string.
   */
  [[nodiscard]] std::string LastTag() const { return last_tag_; }

  // --- XLIST (RFC-unofficial — GMail extension, similar to LIST) ---

  /**
   * @brief XLIST command (GMail / Dovecot extension).
   * @param reference Reference name.
   * @param mailbox_pattern Mailbox pattern (e.g. "*").
   * @return Formatted command string.
   */
  std::string Xlist(std::string_view reference, std::string_view mailbox_pattern);

  // --- MOVE (RFC 6851) ---

  /**
   * @brief MOVE command (RFC 6851).
   * @param sequence_set Sequence set (pre-formatted, e.g. "1:5").
   * @param mailbox Destination mailbox.
   * @param uid If true, emit UID MOVE.
   * @return Formatted command string.
   */
  std::string Move(std::string_view sequence_set, std::string_view mailbox,
                   bool uid = false);

  // --- UNSELECT (RFC 3691) ---

  /**
   * @brief UNSELECT command (RFC 3691): close mailbox without expunging.
   * @return Formatted command string.
   */
  std::string Unselect();

  // --- CONDSTORE (RFC 7162) ---

  /**
   * @brief SELECT with CONDSTORE activation (RFC 7162 §3.1.1).
   * @param mailbox Mailbox name.
   * @return Formatted command string.
   */
  std::string SelectCondStore(std::string_view mailbox);

  /**
   * @brief EXAMINE with CONDSTORE activation.
   * @param mailbox Mailbox name.
   * @return Formatted command string.
   */
  std::string ExamineCondStore(std::string_view mailbox);

  /**
   * @brief FETCH with CHANGEDSINCE modifier (RFC 7162 §3.1.4).
   * @param sequence_set Pre-formatted sequence set.
   * @param items FetchItems bitmask.
   * @param mod_seq Only return messages changed since this MODSEQ.
   * @param uid If true, emit UID FETCH.
   * @return Formatted command string.
   */
  std::string FetchChangedSince(std::string_view sequence_set, FetchItems items,
                                 ModSequence mod_seq, bool uid = false);

  /**
   * @brief STORE with UNCHANGEDSINCE guard (RFC 7162 §3.1.5).
   * @param sequence_set Pre-formatted sequence set.
   * @param operation "+FLAGS", "-FLAGS", or "FLAGS" (or .SILENT variants).
   * @param flags Message flags to set.
   * @param mod_seq Only update if MODSEQ is still this value.
   * @param uid If true, emit UID STORE.
   * @return Formatted command string.
   */
  std::string StoreUnchangedSince(std::string_view sequence_set,
                                   std::string_view operation,
                                   MessageFlags flags, ModSequence mod_seq,
                                   bool uid = false);

  /**
   * @brief SEARCH with MODSEQ criterion (RFC 7162 §3.1.6).
   * @param criteria Base search criteria list.
   * @param mod_seq Minimum MODSEQ value to match.
   * @param uid If true, emit UID SEARCH.
   * @return Formatted command string.
   */
  std::string SearchModSeq(const std::vector<std::string>& criteria,
                            ModSequence mod_seq, bool uid = false);

  // --- QRESYNC (RFC 7162) ---

  /**
   * @brief SELECT with QRESYNC parameters (RFC 7162 §3.2.5).
   * @param mailbox Mailbox name.
   * @param params QRESYNC parameters (uid_validity, last_mod_seq, etc.).
   * @return Formatted command string.
   */
  std::string SelectQResync(std::string_view mailbox, const QResyncParams& params);

  // --- SORT (RFC 5256) ---

  /**
   * @brief SORT command (RFC 5256).
   * @param sort_criteria Sort program list (e.g. {"ARRIVAL"}, {"REVERSE", "DATE"}).
   * @param charset Search charset (e.g. "UTF-8").
   * @param search_criteria Search program keys.
   * @param uid If true, emit UID SORT.
   * @return Formatted command string.
   */
  std::string Sort(const std::vector<std::string>& sort_criteria,
                   std::string_view charset,
                   const std::vector<std::string>& search_criteria,
                   bool uid = false);

  // --- ESEARCH (RFC 4731) ---

  /**
   * @brief SEARCH with RETURN options (RFC 4731 ESEARCH).
   * @param criteria Search criteria list.
   * @param return_opts Return options, e.g. {"MIN", "MAX", "COUNT", "ALL"}.
   * @param uid If true, emit UID SEARCH.
   * @return Formatted command string.
   */
  std::string ESearch(const std::vector<std::string>& criteria,
                       const std::vector<std::string>& return_opts,
                       bool uid = false);

 private:
  std::string NextTag();
  static std::string QuoteString(std::string_view s);
  static std::string FormatMailbox(std::string_view mailbox);
  static std::string FormatFlags(MessageFlags flags);
  static std::string JoinStrings(const std::vector<std::string>& v,
                                  char sep = ' ');

  std::string tag_prefix_;
  uint64_t tag_counter_{0};
  std::string last_tag_;
};

}  // namespace imap

#endif  // IMAP_EXTENSIONS_H_
