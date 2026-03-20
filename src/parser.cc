#include "imap/parser.h"


#ifdef DEBUG
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace imap {

// ---------------------------------------------------------------------------
// Feed / Next (incremental pull-based interface)
// ---------------------------------------------------------------------------

void ResponseParser::Feed(std::string_view data) {
#ifdef DEBUG
  spdlog::debug("ResponseParser::Feed() {} bytes", data.size());
#endif
  buffer_ += data;
}

bool ResponseParser::HasPending() const {
  return buffer_.find("\r\n") != std::string::npos ||
         buffer_.find('\n') != std::string::npos;
}

void ResponseParser::Reset() {
  #ifdef DEBUG
  spdlog::debug("ResponseParser::Reset() called");
  #endif
  buffer_.clear();
  pending_literal_size_.reset();
}

std::optional<ParsedResponse> ResponseParser::Next() {
  #ifdef DEBUG
  spdlog::debug("ResponseParser::Next() called");
  #endif
  auto line_opt = ExtractLine();
  if (!line_opt.has_value()) return std::nullopt;
  #ifdef DEBUG
  spdlog::debug("ResponseParser::Next() got line: {}", *line_opt);
  #endif
  return ParseLine(*line_opt);
}

// ---------------------------------------------------------------------------
// Line extraction (handles literals)
// ---------------------------------------------------------------------------

std::optional<std::string> ResponseParser::ExtractLine() {
  // If we are waiting for a literal body, check whether we have enough bytes.
  if (pending_literal_size_.has_value()) {
    #ifdef DEBUG
    spdlog::debug("ResponseParser::ExtractLine() waiting for literal size: {}", *pending_literal_size_);
    #endif
    if (buffer_.size() < *pending_literal_size_) return std::nullopt;
    // The literal bytes are already appended to the last line in buffer_.
    // We don't extract separately; the caller sees the whole line + literal
    // as part of the ongoing line extraction.  Reset the literal expectation.
    pending_literal_size_.reset();
  }

  // Find CRLF or bare LF.
  auto crlf = buffer_.find("\r\n");
  auto lf   = buffer_.find('\n');
  std::size_t end;
  std::size_t skip;

  if (crlf != std::string::npos &&
      (lf == std::string::npos || crlf < lf)) {
    end  = crlf;
    skip = crlf + 2;
  } else if (lf != std::string::npos) {
    end  = lf;
    skip = lf + 1;
  } else {
    return std::nullopt;
  }

  std::string line = buffer_.substr(0, end);
  buffer_.erase(0, skip);

  // Check whether this line ends with a literal specifier: {N} or {N+}
  // RFC 3501 §4.3: used in server responses for data items.
  if (!line.empty() && line.back() == '}') {
    auto brace = line.rfind('{');
    if (brace != std::string::npos) {
      std::string_view num_sv(line.data() + brace + 1,
                               line.size() - brace - 2);  // strip { and }
      // Allow trailing '+' for LITERAL+ extension.
      if (!num_sv.empty() && num_sv.back() == '+') num_sv.remove_suffix(1);
      std::size_t literal_size{0};
      auto [ptr, ec] = std::from_chars(num_sv.data(),
                                        num_sv.data() + num_sv.size(),
                                        literal_size);
      if (ec == std::errc{}) {
        // We need `literal_size` more bytes before the next CRLF.
        // Consume the literal data and append to this line.
        if (buffer_.size() < literal_size) {
          // Not enough data yet — put the line back.
          buffer_ = line + "\r\n" + buffer_;
          pending_literal_size_ = literal_size;
          return std::nullopt;
        }
        std::string literal_data = buffer_.substr(0, literal_size);
        buffer_.erase(0, literal_size);
        // Skip trailing CRLF after literal.
        if (buffer_.size() >= 2 && buffer_[0] == '\r' && buffer_[1] == '\n')
          buffer_.erase(0, 2);
        else if (!buffer_.empty() && buffer_[0] == '\n')
          buffer_.erase(0, 1);

        // Replace the {N} suffix with the actual literal data.
        line.erase(brace);
        line += literal_data;
      }
    }
  }

  return line;
}

// ---------------------------------------------------------------------------
// Top-level line dispatch
// ---------------------------------------------------------------------------

ParsedResponse ResponseParser::ParseLine(std::string_view line) {
  #ifdef DEBUG
  spdlog::debug("ResponseParser::ParseLine() input: {}", line);
  #endif
  ParsedResponse result;

  if (line.empty()) {
    // Malformed: return an empty untagged.
    #ifdef DEBUG
    spdlog::warn("ResponseParser::ParseLine() got empty line");
    #endif
    result.type = ResponseType::kUntagged;
    result.payload = UntaggedResponse{};
    return result;
  }

  // Continuation response.
  if (line[0] == '+') {
    result.type = ResponseType::kContinuation;
    ContinuationResponse cont;
    cont.text = std::string(
        (line.size() > 2 && line[1] == ' ') ? line.substr(2) : line.substr(1));
    result.payload = std::move(cont);
    return result;
  }

  // Untagged response.
  if (line[0] == '*') {
    result.type = ResponseType::kUntagged;
    std::string_view rest =
        (line.size() > 2 && line[1] == ' ') ? line.substr(2) : line.substr(1);
    result.payload = ParseUntagged(rest);
    return result;
  }

  // Tagged response: "<tag> STATUS …"
  result.type = ResponseType::kTagged;
  std::size_t sp = line.find(' ');
  std::string tag = (sp != std::string_view::npos)
                        ? std::string(line.substr(0, sp))
                        : std::string(line);
  std::string_view after_tag =
      (sp != std::string_view::npos) ? line.substr(sp + 1) : "";

  std::size_t sp2 = after_tag.find(' ');
  std::string_view status_token =
      (sp2 != std::string_view::npos) ? after_tag.substr(0, sp2) : after_tag;
  std::string_view rest_text =
      (sp2 != std::string_view::npos) ? after_tag.substr(sp2 + 1) : "";

  TaggedResponse tagged;
  tagged.tag    = std::move(tag);
  tagged.status = ParseStatusResponse(ToResponseStatus(status_token), rest_text);
  result.payload = std::move(tagged);
  return result;
}

// ---------------------------------------------------------------------------
// Untagged parsers
// ---------------------------------------------------------------------------

UntaggedResponse ResponseParser::ParseUntagged(std::string_view line) {
  UntaggedResponse resp;

  // Check for leading number (e.g. "3 EXISTS", "12 FETCH (…)").
  if (!line.empty() && std::isdigit(static_cast<unsigned char>(line[0]))) {
    std::size_t pos = 0;
    uint32_t number = 0;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
      number = number * 10 + static_cast<uint32_t>(line[pos] - '0');
      ++pos;
    }
    SkipSp(line, pos);
    return ParseNumberedUntagged(number, line.substr(pos));
  }

  // No leading number — keyword-based.
  std::size_t pos = 0;
  std::string keyword = ReadAtom(line, pos);
  SkipSp(line, pos);
  std::string_view rest = line.substr(pos);

  resp.keyword = keyword;

  // Convert keyword to uppercase for comparison.
  std::string kw_upper = keyword;
  std::transform(kw_upper.begin(), kw_upper.end(), kw_upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  if (kw_upper == "OK" || kw_upper == "NO" || kw_upper == "BAD" ||
      kw_upper == "PREAUTH" || kw_upper == "BYE") {
    resp.data = ParseStatusResponse(ToResponseStatus(kw_upper), rest);
  } else if (kw_upper == "CAPABILITY") {
    resp.data = ParseCapability(rest);
  } else if (kw_upper == "FLAGS") {
    resp.data = ParseFlags(rest);
  } else if (kw_upper == "SEARCH") {
    resp.data = ParseSearch(rest);
  } else if (kw_upper == "STATUS") {
    resp.data = ParseStatus(rest);
  } else if (kw_upper == "LIST") {
    resp.data = ParseListLine(false, rest).mailboxes.empty()
                    ? ListData{}
                    : ParseListLine(false, rest);
  } else if (kw_upper == "LSUB") {
    resp.data = ParseListLine(true, rest);
  }
  // Other keywords (e.g. NAMESPACE) result in monostate — extensible.

  return resp;
}

UntaggedResponse ResponseParser::ParseNumberedUntagged(uint32_t number,
                                                        std::string_view rest) {
  UntaggedResponse resp;
  resp.number = number;

  std::size_t pos = 0;
  std::string keyword = ReadAtom(rest, pos);
  SkipSp(rest, pos);
  std::string_view after_kw = rest.substr(pos);

  resp.keyword = keyword;

  std::string kw_upper = keyword;
  std::transform(kw_upper.begin(), kw_upper.end(), kw_upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  if (kw_upper == "EXISTS" || kw_upper == "RECENT" || kw_upper == "EXPUNGE") {
    resp.data = CountData{number};
  } else if (kw_upper == "FETCH") {
    FetchData fd;
    fd.messages.push_back(ParseFetch(number, after_kw));
    resp.data = std::move(fd);
  }
  return resp;
}

// ---------------------------------------------------------------------------
// Status response
// ---------------------------------------------------------------------------

StatusResponse ResponseParser::ParseStatusResponse(ResponseStatus status,
                                                    std::string_view rest) {
  StatusResponse sr;
  sr.status = status;
  sr.code   = ParseResponseCode(rest);

  // `rest` has been trimmed of the code block by ParseResponseCode.
  sr.text = std::string(rest);
  return sr;
}

// ---------------------------------------------------------------------------
// Capability
// ---------------------------------------------------------------------------

CapabilityData ResponseParser::ParseCapability(std::string_view rest) {
  CapabilityData data;
  std::size_t pos = 0;
  while (pos < rest.size()) {
    SkipSp(rest, pos);
    if (pos >= rest.size()) break;
    std::string cap = ReadAtom(rest, pos);
    if (!cap.empty()) data.capabilities.push_back(std::move(cap));
  }
  return data;
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

FlagsData ResponseParser::ParseFlags(std::string_view rest) {
  FlagsData data;
  // rest is like "(\Seen \Answered \Flagged …)"
  auto tokens = TokeniseParenList(rest);
  for (auto& t : tokens) data.flags.push_back(std::move(t));
  return data;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

SearchData ResponseParser::ParseSearch(std::string_view rest) {
  SearchData data;
  std::size_t pos = 0;
  while (pos < rest.size()) {
    SkipSp(rest, pos);
    if (pos >= rest.size()) break;
    std::string num_str = ReadAtom(rest, pos);
    if (num_str.empty()) break;
    uint32_t num = 0;
    auto [ptr, ec] =
        std::from_chars(num_str.data(), num_str.data() + num_str.size(), num);
    if (ec == std::errc{}) data.numbers.push_back(num);
  }
  return data;
}

// ---------------------------------------------------------------------------
// Status (mailbox status response)
// ---------------------------------------------------------------------------

StatusData ResponseParser::ParseStatus(std::string_view rest) {
  StatusData data;
  std::size_t pos = 0;

  // Mailbox name comes first.
  if (pos < rest.size() && rest[pos] == '"') {
    data.mailbox = ReadQuotedString(rest, pos);
  } else {
    data.mailbox = ReadAtom(rest, pos);
  }
  SkipSp(rest, pos);

  // Then parenthesised list of key-value pairs.
  if (pos < rest.size() && rest[pos] == '(') {
    ++pos;  // skip '('
    while (pos < rest.size() && rest[pos] != ')') {
      SkipSp(rest, pos);
      std::string key = ReadAtom(rest, pos);
      SkipSp(rest, pos);
      std::string val_str = ReadAtom(rest, pos);
      SkipSp(rest, pos);
      if (key.empty()) break;
      std::transform(key.begin(), key.end(), key.begin(),
                     [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      uint32_t val = 0;
      std::from_chars(val_str.data(), val_str.data() + val_str.size(), val);
      data.items[key] = val;
    }
  }
  return data;
}

// ---------------------------------------------------------------------------
// List / Lsub
// ---------------------------------------------------------------------------

ListData ResponseParser::ParseListLine(bool /*is_lsub*/, std::string_view rest) {
  ListData data;
  MailboxInfo info;
  std::size_t pos = 0;

  // Attributes list: "(\Noselect \HasNoChildren)"
  if (pos < rest.size() && rest[pos] == '(') {
    auto tokens = TokeniseParenList(rest.substr(pos));
    for (auto& t : tokens) info.attributes.push_back(std::move(t));
    // Advance past the parenthesised list.
    ++pos;  // '('
    int depth = 1;
    while (pos < rest.size() && depth > 0) {
      if (rest[pos] == '(') ++depth;
      else if (rest[pos] == ')') --depth;
      ++pos;
    }
  }
  SkipSp(rest, pos);

  // Hierarchy delimiter (quoted or NIL).
  if (pos < rest.size() && rest[pos] == '"') {
    info.hierarchy_delimiter = ReadQuotedString(rest, pos);
  } else {
    std::string atom = ReadAtom(rest, pos);
    if (atom != "NIL") info.hierarchy_delimiter = atom;
  }
  SkipSp(rest, pos);

  // Mailbox name.
  if (pos < rest.size() && rest[pos] == '"') {
    info.name = ReadQuotedString(rest, pos);
  } else {
    info.name = ReadAtom(rest, pos);
  }

  data.mailboxes.push_back(std::move(info));
  return data;
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

FetchedMessage ResponseParser::ParseFetch(uint32_t seq_num,
                                           std::string_view rest) {
  FetchedMessage msg;
  msg.sequence_number = seq_num;

  // rest is like "(UID 123 FLAGS (\Seen) …)"
  // Strip outer parens.
  std::string_view inner = rest;
  if (!inner.empty() && inner.front() == '(') inner.remove_prefix(1);
  if (!inner.empty() && inner.back() == ')') inner.remove_suffix(1);

  std::size_t pos = 0;
  while (pos < inner.size()) {
    SkipSp(inner, pos);
    if (pos >= inner.size()) break;

    std::string attr = ReadAtom(inner, pos);
    if (attr.empty()) { ++pos; continue; }
    std::transform(attr.begin(), attr.end(), attr.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    SkipSp(inner, pos);

    if (attr == "UID") {
      std::string val = ReadAtom(inner, pos);
      uint32_t uid = 0;
      std::from_chars(val.data(), val.data() + val.size(), uid);
      msg.uid = uid;
    } else if (attr == "FLAGS") {
      // Parenthesised flag list.
      std::size_t start = pos;
      if (pos < inner.size() && inner[pos] == '(') {
        int depth = 1; ++pos;
        while (pos < inner.size() && depth > 0) {
          if (inner[pos] == '(') ++depth;
          else if (inner[pos] == ')') --depth;
          ++pos;
        }
        msg.flags = ParseFlagList(inner.substr(start, pos - start));
      }
    } else if (attr == "RFC822.SIZE") {
      std::string val = ReadAtom(inner, pos);
      uint64_t sz = 0;
      std::from_chars(val.data(), val.data() + val.size(), sz);
      msg.rfc822_size = sz;
    } else if (attr == "INTERNALDATE") {
      if (pos < inner.size() && inner[pos] == '"') {
        msg.internal_date = ReadQuotedString(inner, pos);
      } else {
        msg.internal_date = ReadAtom(inner, pos);
      }
    } else if (attr == "RFC822.HEADER" || attr == "RFC822.TEXT" ||
               attr == "RFC822") {
      // The data follows directly (already extracted from literal by ExtractLine).
      auto val_opt = ReadNilOrString(inner, pos);
      if (val_opt.has_value()) {
        if (attr == "RFC822.HEADER") msg.rfc822_header = std::move(*val_opt);
        else if (attr == "RFC822.TEXT") msg.rfc822_text = std::move(*val_opt);
        else msg.rfc822 = std::move(*val_opt);
      }
    } else if (attr == "ENVELOPE") {
      // Find matching parentheses.
      if (pos < inner.size() && inner[pos] == '(') {
        std::size_t start = pos;
        int depth = 1; ++pos;
        while (pos < inner.size() && depth > 0) {
          if (inner[pos] == '(') ++depth;
          else if (inner[pos] == ')') --depth;
          ++pos;
        }
        msg.envelope = ParseEnvelope(inner.substr(start, pos - start));
      }
    } else if (attr == "BODYSTRUCTURE" || attr == "BODY") {
      if (pos < inner.size() && inner[pos] == '(') {
        std::size_t start = pos;
        int depth = 1; ++pos;
        while (pos < inner.size() && depth > 0) {
          if (inner[pos] == '(') ++depth;
          else if (inner[pos] == ')') --depth;
          ++pos;
        }
        msg.body_structure = ParseBodyStructure(inner.substr(start, pos - start));
      } else if (attr.find("BODY[") == 0 || attr.find('[') != std::string::npos) {
        // BODY[section] — key ends at ']', possibly with <offset>.
        auto val_opt = ReadNilOrString(inner, pos);
        if (val_opt.has_value()) msg.body_sections[attr] = std::move(*val_opt);
      }
    }
    // Unknown attributes: skip to next space.
  }
  return msg;
}

// ---------------------------------------------------------------------------
// Envelope / address list
// ---------------------------------------------------------------------------

Envelope ResponseParser::ParseEnvelope(std::string_view paren_data) {
  Envelope env;
  // Strip outer parens.
  std::string_view inner = paren_data;
  if (!inner.empty() && inner.front() == '(') inner.remove_prefix(1);
  if (!inner.empty() && inner.back() == ')') inner.remove_suffix(1);

  // Fixed order per RFC 3501 §7.4.2:
  // date subject from sender reply-to to cc bcc in-reply-to message-id
  std::size_t pos = 0;
  auto read = [&]() -> std::optional<std::string> {
    return ReadNilOrString(inner, pos);
  };
  auto read_addr_list = [&]() -> std::vector<AddressInfo> {
    SkipSp(inner, pos);
    if (pos >= inner.size()) return {};
    if (inner[pos] == '(') {
      std::size_t start = pos;
      int depth = 1; ++pos;
      while (pos < inner.size() && depth > 0) {
        if (inner[pos] == '(') ++depth;
        else if (inner[pos] == ')') --depth;
        ++pos;
      }
      return ParseAddressList(inner.substr(start, pos - start));
    }
    // NIL
    ReadAtom(inner, pos);
    return {};
  };

  env.date       = read(); SkipSp(inner, pos);
  env.subject    = read(); SkipSp(inner, pos);
  env.from       = read_addr_list();
  env.sender     = read_addr_list();
  env.reply_to   = read_addr_list();
  env.to         = read_addr_list();
  env.cc         = read_addr_list();
  env.bcc        = read_addr_list();
  SkipSp(inner, pos);
  env.in_reply_to = read();
  SkipSp(inner, pos);
  env.message_id  = read();
  return env;
}

std::vector<AddressInfo> ResponseParser::ParseAddressList(
    std::string_view paren_data) {
  std::vector<AddressInfo> list;
  // Outer parens enclose N address structs, each in its own parens.
  std::string_view inner = paren_data;
  if (!inner.empty() && inner.front() == '(') inner.remove_prefix(1);
  if (!inner.empty() && inner.back() == ')') inner.remove_suffix(1);

  std::size_t pos = 0;
  while (pos < inner.size()) {
    SkipSp(inner, pos);
    if (pos >= inner.size()) break;
    if (inner[pos] != '(') { ++pos; continue; }
    std::size_t start = pos;
    int depth = 1; ++pos;
    while (pos < inner.size() && depth > 0) {
      if (inner[pos] == '(') ++depth;
      else if (inner[pos] == ')') --depth;
      ++pos;
    }
    list.push_back(ParseAddress(inner.substr(start, pos - start)));
  }
  return list;
}

AddressInfo ResponseParser::ParseAddress(std::string_view paren_data) {
  AddressInfo ai;
  std::string_view inner = paren_data;
  if (!inner.empty() && inner.front() == '(') inner.remove_prefix(1);
  if (!inner.empty() && inner.back() == ')') inner.remove_suffix(1);

  std::size_t pos = 0;
  auto read = [&]() -> std::optional<std::string> {
    SkipSp(inner, pos);
    return ReadNilOrString(inner, pos);
  };
  ai.display_name = read();
  ai.source_route = read();
  ai.mailbox      = read();
  ai.host         = read();
  return ai;
}

// ---------------------------------------------------------------------------
// Body structure (simplified)
// ---------------------------------------------------------------------------

BodyStructurePart ResponseParser::ParseBodyStructure(
    std::string_view paren_data) {
  BodyStructurePart part;
  std::string_view inner = paren_data;
  if (!inner.empty() && inner.front() == '(') inner.remove_prefix(1);
  if (!inner.empty() && inner.back() == ')') inner.remove_suffix(1);

  std::size_t pos = 0;
  SkipSp(inner, pos);

  // Multipart check: first element is itself a parenthesised list.
  if (pos < inner.size() && inner[pos] == '(') {
    part.media_type = "MULTIPART";
    // Parse sub-parts.
    while (pos < inner.size() && inner[pos] == '(') {
      std::size_t start = pos;
      int depth = 1; ++pos;
      while (pos < inner.size() && depth > 0) {
        if (inner[pos] == '(') ++depth;
        else if (inner[pos] == ')') --depth;
        ++pos;
      }
      part.parts.push_back(ParseBodyStructure(inner.substr(start, pos - start)));
      SkipSp(inner, pos);
    }
    // Subtype follows.
    auto subtype_opt = ReadNilOrString(inner, pos);
    part.subtype = subtype_opt.value_or("MIXED");
    return part;
  }

  // Basic body part: type subtype (params) id desc encoding size …
  auto read_str = [&]() -> std::optional<std::string> {
    SkipSp(inner, pos);
    return ReadNilOrString(inner, pos);
  };

  auto type    = read_str(); part.media_type = type.value_or("TEXT");
  auto subtype = read_str(); part.subtype    = subtype.value_or("PLAIN");

  // Parameters list.
  SkipSp(inner, pos);
  if (pos < inner.size() && inner[pos] == '(') {
    auto tokens = TokeniseParenList(inner.substr(pos));
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
      std::string key = tokens[i];
      std::transform(key.begin(), key.end(), key.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      part.parameters[key] = tokens[i + 1];
    }
    // Advance past the paren list.
    int depth = 1; ++pos;
    while (pos < inner.size() && depth > 0) {
      if (inner[pos] == '(') ++depth;
      else if (inner[pos] == ')') --depth;
      ++pos;
    }
  } else {
    // NIL
    ReadAtom(inner, pos);
  }

  auto id   = read_str(); part.content_id  = id;
  auto desc = read_str(); part.description = desc;
  auto enc  = read_str(); part.encoding    = enc.value_or("7BIT");

  SkipSp(inner, pos);
  std::string sz_str = ReadAtom(inner, pos);
  uint64_t sz = 0;
  std::from_chars(sz_str.data(), sz_str.data() + sz_str.size(), sz);
  part.size = sz;

  return part;
}

// ---------------------------------------------------------------------------
// Response code parser
// ---------------------------------------------------------------------------

ResponseCodeData ResponseParser::ParseResponseCode(std::string_view& text) {
  ResponseCodeData code;
  // Trim leading whitespace.
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);

  if (text.empty() || text.front() != '[') return code;

  auto end = text.find(']');
  if (end == std::string_view::npos) return code;

  std::string_view code_block = text.substr(1, end - 1);
  text = text.substr(end + 1);
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);

  std::size_t sp = code_block.find(' ');
  std::string_view code_token =
      (sp != std::string_view::npos) ? code_block.substr(0, sp) : code_block;

  code.raw_code  = std::string(code_token);
  code.arguments = (sp != std::string_view::npos)
                       ? std::string(code_block.substr(sp + 1))
                       : "";

  std::string upper = code.raw_code;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  code.code = ToResponseCode(upper);
  return code;
}

// ---------------------------------------------------------------------------
// Flag list parser (parenthesised)
// ---------------------------------------------------------------------------

MessageFlags ResponseParser::ParseFlagList(std::string_view paren_list) {
  MessageFlags flags = 0;
  auto tokens = TokeniseParenList(paren_list);
  for (const auto& t : tokens) {
    std::string upper = t;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "\\SEEN")     flags |= static_cast<MessageFlags>(MessageFlag::kSeen);
    if (upper == "\\ANSWERED") flags |= static_cast<MessageFlags>(MessageFlag::kAnswered);
    if (upper == "\\FLAGGED")  flags |= static_cast<MessageFlags>(MessageFlag::kFlagged);
    if (upper == "\\DELETED")  flags |= static_cast<MessageFlags>(MessageFlag::kDeleted);
    if (upper == "\\DRAFT")    flags |= static_cast<MessageFlags>(MessageFlag::kDraft);
    if (upper == "\\RECENT")   flags |= static_cast<MessageFlags>(MessageFlag::kRecent);
  }
  return flags;
}

// ---------------------------------------------------------------------------
// Tokeniser
// ---------------------------------------------------------------------------

std::vector<std::string> ResponseParser::TokeniseParenList(
    std::string_view input) {
  std::vector<std::string> tokens;
  std::size_t pos = 0;
  if (pos < input.size() && input[pos] == '(') ++pos;

  while (pos < input.size() && input[pos] != ')') {
    SkipSp(input, pos);
    if (pos >= input.size() || input[pos] == ')') break;

    if (input[pos] == '"') {
      tokens.push_back(ReadQuotedString(input, pos));
    } else if (input[pos] == '(') {
      // Nested list — treat as a single opaque token.
      std::size_t start = pos;
      int depth = 1; ++pos;
      while (pos < input.size() && depth > 0) {
        if (input[pos] == '(') ++depth;
        else if (input[pos] == ')') --depth;
        ++pos;
      }
      tokens.push_back(std::string(input.substr(start, pos - start)));
    } else {
      tokens.push_back(ReadAtom(input, pos));
    }
  }
  return tokens;
}

// ---------------------------------------------------------------------------
// Primitive readers
// ---------------------------------------------------------------------------

std::string ResponseParser::ReadQuotedString(std::string_view input,
                                              std::size_t& pos) {
  std::string result;
  if (pos >= input.size() || input[pos] != '"') return result;
  ++pos;  // skip opening quote
  while (pos < input.size() && input[pos] != '"') {
    if (input[pos] == '\\' && pos + 1 < input.size()) {
      ++pos;  // skip backslash
      result += input[pos];
    } else {
      result += input[pos];
    }
    ++pos;
  }
  if (pos < input.size()) ++pos;  // skip closing quote
  return result;
}

std::string ResponseParser::ReadAtom(std::string_view input,
                                      std::size_t& pos) {
  std::string result;
  while (pos < input.size()) {
    char c = input[pos];
    if (c == ' ' || c == '\t' || c == '(' || c == ')' || c == '"' ||
        c == '{' || c == '\r' || c == '\n')
      break;
    result += c;
    ++pos;
  }
  return result;
}

std::optional<std::string> ResponseParser::ReadNilOrString(
    std::string_view input, std::size_t& pos) {
  SkipSp(input, pos);
  if (pos >= input.size()) return std::nullopt;
  if (input[pos] == '"') return ReadQuotedString(input, pos);
  std::string atom = ReadAtom(input, pos);
  std::string upper = atom;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (upper == "NIL") return std::nullopt;
  return atom;
}

void ResponseParser::SkipSp(std::string_view input, std::size_t& pos) {
  while (pos < input.size() &&
         (input[pos] == ' ' || input[pos] == '\t'))
    ++pos;
}

// ---------------------------------------------------------------------------
// Enum converters
// ---------------------------------------------------------------------------

ResponseStatus ResponseParser::ToResponseStatus(std::string_view token) {
  std::string upper(token);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (upper == "OK")      return ResponseStatus::kOk;
  if (upper == "NO")      return ResponseStatus::kNo;
  if (upper == "BAD")     return ResponseStatus::kBad;
  if (upper == "PREAUTH") return ResponseStatus::kPreauth;
  if (upper == "BYE")     return ResponseStatus::kBye;
  return ResponseStatus::kBad;
}

ResponseCode ResponseParser::ToResponseCode(std::string_view token) {
  if (token == "ALERT")           return ResponseCode::kAlert;
  if (token == "ALREADYEXISTS")   return ResponseCode::kAlreadyExists;
  if (token == "APPENDUID")       return ResponseCode::kAppendUid;
  if (token == "AUTHENTICATIONFAILED") return ResponseCode::kAuthenticationFailed;
  if (token == "BADCHARSET")      return ResponseCode::kBadCharset;
  if (token == "CAPABILITY")      return ResponseCode::kCapability;
  if (token == "COPYUID")         return ResponseCode::kCopyUid;
  if (token == "EXPUNGEISSUED")   return ResponseCode::kExpungeIssued;
  if (token == "INUSE")           return ResponseCode::kInUse;
  if (token == "LIMIT")           return ResponseCode::kLimit;
  if (token == "NONEXISTENT")     return ResponseCode::kNonExistent;
  if (token == "NOTEXISTENT")     return ResponseCode::kNotExistent;
  if (token == "OVERQUOTA")       return ResponseCode::kOverQuota;
  if (token == "PARSE")           return ResponseCode::kParse;
  if (token == "PERMANENTFLAGS")  return ResponseCode::kPermanentFlags;
  if (token == "READ-ONLY")       return ResponseCode::kReadOnly;
  if (token == "READ-WRITE")      return ResponseCode::kReadWrite;
  if (token == "TRYCREATE")       return ResponseCode::kTryCreate;
  if (token == "UIDNEXT")         return ResponseCode::kUidNext;
  if (token == "UIDVALIDITY")     return ResponseCode::kUidValidity;
  if (token == "UNSEEN")          return ResponseCode::kUnseen;
  return ResponseCode::kUnknown;
}

}  // namespace imap
