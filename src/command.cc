#include "imap/command.h"


#ifdef DEBUG
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <format>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace imap {

// ---------------------------------------------------------------------------
// CommandBuilder
// ---------------------------------------------------------------------------

CommandBuilder::CommandBuilder(std::string_view tag_prefix)
    : tag_prefix_(tag_prefix) {}

void CommandBuilder::Reset() {
  tag_counter_ = 0;
  last_tag_.clear();
#ifdef DEBUG
  spdlog::debug("CommandBuilder::Reset() called");
#endif
}

std::string CommandBuilder::NextTag() {
  ++tag_counter_;
  last_tag_ = std::format("{}{}", tag_prefix_, tag_counter_);
#ifdef DEBUG
  spdlog::debug("Generated new tag: {}", last_tag_);
#endif
  return last_tag_;
}

// Escape a string as an IMAP quoted-string (RFC 3501 §4.3).
// Returns the quoted form.  Characters that are not permitted inside a
// quoted-string are encoded as literals (caller must handle "{N+}\r\n" style
// for those; here we just escape the common ones).
std::string CommandBuilder::QuoteString(std::string_view s) {
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

std::string CommandBuilder::FormatMailbox(std::string_view mailbox) {
  // If the mailbox name contains special chars, quote it.
  for (char c : mailbox) {
    if (c == ' ' || c == '"' || c == '\\' || c == '(' || c == ')' ||
        c == '{' || c == '%' || c == '*')
      return QuoteString(mailbox);
  }
  return std::string(mailbox);
}

std::string CommandBuilder::FormatFlags(MessageFlags flags) {
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

std::string CommandBuilder::FormatFetchItems(
    FetchItems items, const std::vector<BodySection>& sections) {
  // Handle macro shorthands first.
  if (items & static_cast<FetchItems>(FetchItem::kAll)) return "ALL";
  if (items & static_cast<FetchItems>(FetchItem::kFull)) return "FULL";
  if (items & static_cast<FetchItems>(FetchItem::kFast)) return "FAST";

  std::string s = "(";
  auto add = [&](FetchItem fi, std::string_view name) {
    if (items & static_cast<FetchItems>(fi)) { s += name; s += ' '; }
  };
  add(FetchItem::kEnvelope,       "ENVELOPE");
  add(FetchItem::kFlags,          "FLAGS");
  add(FetchItem::kInternalDate,   "INTERNALDATE");
  add(FetchItem::kRfc822Size,     "RFC822.SIZE");
  add(FetchItem::kRfc822Header,   "RFC822.HEADER");
  add(FetchItem::kRfc822Text,     "RFC822.TEXT");
  add(FetchItem::kRfc822,         "RFC822");
  add(FetchItem::kBody,           "BODY");
  add(FetchItem::kBodyStructure,  "BODYSTRUCTURE");
  add(FetchItem::kUid,            "UID");

  for (const auto& sec : sections) {
    s += "BODY[";
    s += sec.section;
    s += ']';
    if (sec.offset.has_value()) {
      s += std::format("<{}.{}>", *sec.offset, sec.size.value_or(0));
    }
    s += ' ';
  }

  if (s.back() == ' ') s.pop_back();
  s += ')';
  return s;
}

std::string CommandBuilder::FormatStatusItems(
    const std::vector<StatusItem>& items) {
  std::string s = "(";
  for (auto item : items) {
    switch (item) {
      case StatusItem::kMessages:    s += "MESSAGES ";    break;
      case StatusItem::kRecent:      s += "RECENT ";      break;
      case StatusItem::kUidNext:     s += "UIDNEXT ";     break;
      case StatusItem::kUidValidity: s += "UIDVALIDITY "; break;
      case StatusItem::kUnseen:      s += "UNSEEN ";      break;
    }
  }
  if (s.back() == ' ') s.pop_back();
  s += ')';
  return s;
}

// ---------------------------------------------------------------------------
// Command methods — each returns a complete IMAP command line (with "tag
// COMMAND args\r\n" format).
// ---------------------------------------------------------------------------

std::string CommandBuilder::Capability() {
  return std::format("{} CAPABILITY\r\n", NextTag());
}

std::string CommandBuilder::Noop() {
  return std::format("{} NOOP\r\n", NextTag());
}

std::string CommandBuilder::Logout() {
  return std::format("{} LOGOUT\r\n", NextTag());
}

std::string CommandBuilder::StartTls() {
  return std::format("{} STARTTLS\r\n", NextTag());
}

std::string CommandBuilder::Authenticate(std::string_view mechanism) {
  return std::format("{} AUTHENTICATE {}\r\n", NextTag(), mechanism);
}

std::string CommandBuilder::Login(std::string_view userid,
                                   std::string_view password) {
  return std::format("{} LOGIN {} {}\r\n", NextTag(), QuoteString(userid),
                     QuoteString(password));
}

std::string CommandBuilder::Select(std::string_view mailbox) {
  return std::format("{} SELECT {}\r\n", NextTag(), FormatMailbox(mailbox));
}

std::string CommandBuilder::Examine(std::string_view mailbox) {
  return std::format("{} EXAMINE {}\r\n", NextTag(), FormatMailbox(mailbox));
}

std::string CommandBuilder::Create(std::string_view mailbox) {
  return std::format("{} CREATE {}\r\n", NextTag(), FormatMailbox(mailbox));
}

std::string CommandBuilder::Delete(std::string_view mailbox) {
  return std::format("{} DELETE {}\r\n", NextTag(), FormatMailbox(mailbox));
}

std::string CommandBuilder::Rename(std::string_view existing_name,
                                    std::string_view new_name) {
  return std::format("{} RENAME {} {}\r\n", NextTag(),
                     FormatMailbox(existing_name), FormatMailbox(new_name));
}

std::string CommandBuilder::Subscribe(std::string_view mailbox) {
  return std::format("{} SUBSCRIBE {}\r\n", NextTag(), FormatMailbox(mailbox));
}

std::string CommandBuilder::Unsubscribe(std::string_view mailbox) {
  return std::format("{} UNSUBSCRIBE {}\r\n", NextTag(),
                     FormatMailbox(mailbox));
}

std::string CommandBuilder::List(std::string_view reference,
                                  std::string_view mailbox_pattern) {
  return std::format("{} LIST {} {}\r\n", NextTag(), QuoteString(reference),
                     QuoteString(mailbox_pattern));
}

std::string CommandBuilder::Lsub(std::string_view reference,
                                  std::string_view mailbox_pattern) {
  return std::format("{} LSUB {} {}\r\n", NextTag(), QuoteString(reference),
                     QuoteString(mailbox_pattern));
}

std::string CommandBuilder::Status(std::string_view mailbox,
                                    const std::vector<StatusItem>& items) {
  return std::format("{} STATUS {} {}\r\n", NextTag(), FormatMailbox(mailbox),
                     FormatStatusItems(items));
}

std::string CommandBuilder::Append(std::string_view mailbox,
                                    uint64_t literal_size,
                                    std::optional<MessageFlags> flags,
                                    std::optional<std::string_view> date_time) {
  std::string cmd = std::format("{} APPEND {}", NextTag(),
                                FormatMailbox(mailbox));
  if (flags.has_value()) cmd += ' ' + FormatFlags(*flags);
  if (date_time.has_value())
    cmd += ' ' + QuoteString(*date_time);
  cmd += std::format(" {{{}}}\r\n", literal_size);
  return cmd;
}

std::string CommandBuilder::Check() {
  return std::format("{} CHECK\r\n", NextTag());
}

std::string CommandBuilder::Close() {
  return std::format("{} CLOSE\r\n", NextTag());
}

std::string CommandBuilder::Expunge() {
  return std::format("{} EXPUNGE\r\n", NextTag());
}

std::string CommandBuilder::Search(const std::vector<std::string>& criteria,
                                    bool uid) {
  std::string crit;
  for (std::size_t i = 0; i < criteria.size(); ++i) {
    if (i) crit += ' ';
    crit += criteria[i];
  }
  return std::format("{} {}SEARCH {}\r\n", NextTag(), uid ? "UID " : "",
                     crit);
}

std::string CommandBuilder::Fetch(std::string_view sequence_set,
                                   FetchItems items,
                                   const std::vector<BodySection>& sections,
                                   bool uid) {
  return std::format("{} {}FETCH {} {}\r\n", NextTag(), uid ? "UID " : "",
                     sequence_set, FormatFetchItems(items, sections));
}

std::string CommandBuilder::Store(std::string_view sequence_set,
                                   std::string_view operation,
                                   MessageFlags flags, bool uid) {
  return std::format("{} {}STORE {} {} {}\r\n", NextTag(), uid ? "UID " : "",
                     sequence_set, operation, FormatFlags(flags));
}

std::string CommandBuilder::Copy(std::string_view sequence_set,
                                  std::string_view mailbox, bool uid) {
  return std::format("{} {}COPY {} {}\r\n", NextTag(), uid ? "UID " : "",
                     sequence_set, FormatMailbox(mailbox));
}

std::string CommandBuilder::Idle() {
  return std::format("{} IDLE\r\n", NextTag());
}

std::string CommandBuilder::Done() { return "DONE\r\n"; }

// ---------------------------------------------------------------------------
// SequenceSetBuilder
// ---------------------------------------------------------------------------

SequenceSetBuilder& SequenceSetBuilder::Add(uint32_t number) {
  parts_.push_back(std::to_string(number));
  return *this;
}

SequenceSetBuilder& SequenceSetBuilder::AddRange(uint32_t from, uint32_t to) {
  parts_.push_back(std::format("{}:{}", from, to));
  return *this;
}

SequenceSetBuilder& SequenceSetBuilder::AddWildcard() {
  parts_.push_back("*");
  return *this;
}

std::string SequenceSetBuilder::Build() const {
  std::string result;
  for (std::size_t i = 0; i < parts_.size(); ++i) {
    if (i) result += ',';
    result += parts_[i];
  }
  return result;
}

void SequenceSetBuilder::Clear() { parts_.clear(); }

}  // namespace imap
