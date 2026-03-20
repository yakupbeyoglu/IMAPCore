#include "imap/extensions.h"

#ifdef DEBUG
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <cassert>
#include <format>
#include <numeric>
#include <sstream>

namespace imap {

// ---------------------------------------------------------------------------
// SpecialUseFlagFromString
// ---------------------------------------------------------------------------

SpecialUseFlags SpecialUseFlagFromString(std::string_view attribute) {
  // Normalise: lowercase, strip leading backslash.
  std::string s(attribute);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (!s.empty() && s.front() == '\\') s.erase(s.begin());

  if (s == "all")     return static_cast<SpecialUseFlags>(SpecialUseFlag::kAll);
  if (s == "archive") return static_cast<SpecialUseFlags>(SpecialUseFlag::kArchive);
  if (s == "drafts")  return static_cast<SpecialUseFlags>(SpecialUseFlag::kDrafts);
  if (s == "flagged") return static_cast<SpecialUseFlags>(SpecialUseFlag::kFlagged);
  if (s == "junk")    return static_cast<SpecialUseFlags>(SpecialUseFlag::kJunk);
  if (s == "sent")    return static_cast<SpecialUseFlags>(SpecialUseFlag::kSent);
  if (s == "trash")   return static_cast<SpecialUseFlags>(SpecialUseFlag::kTrash);
  return static_cast<SpecialUseFlags>(SpecialUseFlag::kNone);
}

// ---------------------------------------------------------------------------
// ExtensionCommandBuilder — private helpers
// ---------------------------------------------------------------------------

ExtensionCommandBuilder::ExtensionCommandBuilder(std::string_view tag_prefix)
    : tag_prefix_(tag_prefix) {}

void ExtensionCommandBuilder::Reset() {
  tag_counter_ = 0;
  last_tag_.clear();
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder::Reset() called");
#endif
}

std::string ExtensionCommandBuilder::NextTag() {
  ++tag_counter_;
  last_tag_ = std::format("{}{}", tag_prefix_, tag_counter_);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder::NextTag() generated tag: {}", last_tag_);
#endif
  return last_tag_;
}

std::string ExtensionCommandBuilder::QuoteString(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

std::string ExtensionCommandBuilder::FormatMailbox(std::string_view mailbox) {
  for (char c : mailbox) {
    if (c == ' ' || c == '"' || c == '\\' || c == '(' || c == ')' ||
        c == '{' || c == '%' || c == '*')
      return QuoteString(mailbox);
  }
  return std::string(mailbox);
}

std::string ExtensionCommandBuilder::FormatFlags(MessageFlags flags) {
  std::string s = "(";
  if (HasFlag(flags, MessageFlag::kSeen))     s += "\\Seen ";
  if (HasFlag(flags, MessageFlag::kAnswered)) s += "\\Answered ";
  if (HasFlag(flags, MessageFlag::kFlagged))  s += "\\Flagged ";
  if (HasFlag(flags, MessageFlag::kDeleted))  s += "\\Deleted ";
  if (HasFlag(flags, MessageFlag::kDraft))    s += "\\Draft ";
  if (HasFlag(flags, MessageFlag::kRecent))   s += "\\Recent ";
  if (s.back() == ' ') s.pop_back();
  s += ')';
  return s;
}

std::string ExtensionCommandBuilder::JoinStrings(const std::vector<std::string>& v,
                                                   char sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i > 0) out += sep;
    out += v[i];
  }
  return out;
}

// ---------------------------------------------------------------------------
// XLIST
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::Xlist(std::string_view reference,
                                             std::string_view mailbox_pattern) {
  std::string cmd = std::format("{} XLIST {} {}\r\n", NextTag(),
                                 QuoteString(reference),
                                 QuoteString(mailbox_pattern));
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// MOVE (RFC 6851)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::Move(std::string_view sequence_set,
                                           std::string_view mailbox, bool uid) {
  std::string cmd = std::format("{} {}{} {}\r\n", NextTag(),
                                 uid ? "UID MOVE " : "MOVE ",
                                 sequence_set,
                                 FormatMailbox(mailbox));
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// UNSELECT (RFC 3691)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::Unselect() {
  std::string cmd = std::format("{} UNSELECT\r\n", NextTag());
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// CONDSTORE — SELECT / EXAMINE with (CONDSTORE)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::SelectCondStore(std::string_view mailbox) {
  std::string cmd = std::format("{} SELECT {} (CONDSTORE)\r\n", NextTag(),
                                 FormatMailbox(mailbox));
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

std::string ExtensionCommandBuilder::ExamineCondStore(std::string_view mailbox) {
  std::string cmd = std::format("{} EXAMINE {} (CONDSTORE)\r\n", NextTag(),
                                 FormatMailbox(mailbox));
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// CONDSTORE — FETCH CHANGEDSINCE
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::FetchChangedSince(std::string_view sequence_set,
                                                         FetchItems items,
                                                         ModSequence mod_seq,
                                                         bool uid) {
  // Build a minimal FETCH items string — just include ALL for brevity.
  // Callers needing specific items should extend this.
  (void)items;  // TODO: format items properly
  std::string cmd = std::format("{} {}FETCH {} (FLAGS UID RFC822.SIZE) "
                                "(CHANGEDSINCE {})\r\n",
                                NextTag(),
                                uid ? "UID " : "",
                                sequence_set,
                                mod_seq);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// CONDSTORE — STORE UNCHANGEDSINCE
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::StoreUnchangedSince(
    std::string_view sequence_set, std::string_view operation,
    MessageFlags flags, ModSequence mod_seq, bool uid) {
  std::string cmd = std::format("{} {}STORE {} (UNCHANGEDSINCE {}) {} {}\r\n",
                                NextTag(),
                                uid ? "UID " : "",
                                sequence_set,
                                mod_seq,
                                operation,
                                FormatFlags(flags));
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// CONDSTORE — SEARCH with MODSEQ
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::SearchModSeq(
    const std::vector<std::string>& criteria, ModSequence mod_seq, bool uid) {
  std::string criteria_str = JoinStrings(criteria);
  std::string cmd = std::format("{} {}SEARCH {} MODSEQ {}\r\n",
                                NextTag(),
                                uid ? "UID " : "",
                                criteria_str,
                                mod_seq);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// QRESYNC — SELECT (QRESYNC ...)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::SelectQResync(std::string_view mailbox,
                                                    const QResyncParams& params) {
  std::string qresync = std::format("(QRESYNC ({} {}", params.uid_validity,
                                     params.last_mod_seq);
  if (params.known_uid_set.has_value()) {
    qresync += " " + *params.known_uid_set;
  }
  if (params.seq_match_data.has_value()) {
    qresync += " (" + *params.seq_match_data + ")";
  }
  qresync += "))";

  std::string cmd = std::format("{} SELECT {} {}\r\n", NextTag(),
                                 FormatMailbox(mailbox), qresync);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// SORT (RFC 5256)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::Sort(const std::vector<std::string>& sort_criteria,
                                           std::string_view charset,
                                           const std::vector<std::string>& search_criteria,
                                           bool uid) {
  std::string sort_str = "(" + JoinStrings(sort_criteria) + ")";
  std::string search_str = JoinStrings(search_criteria);
  std::string cmd = std::format("{} {}SORT {} {} {}\r\n",
                                NextTag(),
                                uid ? "UID " : "",
                                sort_str,
                                charset,
                                search_str);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

// ---------------------------------------------------------------------------
// ESEARCH (RFC 4731)
// ---------------------------------------------------------------------------

std::string ExtensionCommandBuilder::ESearch(const std::vector<std::string>& criteria,
                                              const std::vector<std::string>& return_opts,
                                              bool uid) {
  std::string opts = "RETURN (" + JoinStrings(return_opts) + ")";
  std::string search_str = JoinStrings(criteria);
  std::string cmd = std::format("{} {}SEARCH {} {}\r\n",
                                NextTag(),
                                uid ? "UID " : "",
                                opts,
                                search_str);
#ifdef DEBUG
  spdlog::debug("ExtensionCommandBuilder: {}", cmd);
#endif
  return cmd;
}

}  // namespace imap
