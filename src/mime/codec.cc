#include "imap/mime/codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace imap::mime {

std::string DecodeBase64(std::string_view input) {
  // RFC 4648 Base64 alphabet lookup table (value -1 = ignore).
  static constexpr std::array<int8_t, 256> kTable = []() {
    std::array<int8_t, 256> t{};
    t.fill(-1);
    for (int i = 0; i < 26; ++i)
      t[static_cast<std::size_t>('A' + i)] = static_cast<int8_t>(i);
    for (int i = 0; i < 26; ++i)
      t[static_cast<std::size_t>('a' + i)] = static_cast<int8_t>(26 + i);
    for (int i = 0; i < 10; ++i)
      t[static_cast<std::size_t>('0' + i)] = static_cast<int8_t>(52 + i);
    t[static_cast<std::size_t>('+')] = 62;
    t[static_cast<std::size_t>('/')] = 63;
    return t;
  }();

  std::string out;
  out.reserve(input.size() * 3 / 4);

  uint32_t buf  = 0;
  int      bits = 0;

  for (auto ch : input) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isspace(static_cast<int>(c))) continue;
    if (c == '=') break;
    int8_t val = kTable[c];
    if (val < 0) continue;
    buf   = (buf << 6) | static_cast<uint32_t>(val);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xFF);
    }
  }
  return out;
}

std::string DecodeQuotedPrintable(std::string_view input) {
  std::string out;
  out.reserve(input.size());

  for (std::size_t i = 0; i < input.size(); ) {
    char c = input[i];
    if (c == '=' && i + 2 < input.size()) {
      char h1 = input[i + 1];
      char h2 = input[i + 2];
      // Soft line break.
      if (h1 == '\r' && h2 == '\n') { i += 3; continue; }
      if (h1 == '\n')               { i += 2; continue; }

      auto hex_val = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
      };
      int v1 = hex_val(h1);
      int v2 = hex_val(h2);
      if (v1 >= 0 && v2 >= 0) {
        out += static_cast<char>((v1 << 4) | v2);
        i += 3;
        continue;
      }
    }
    out += c;
    ++i;
  }
  return out;
}

std::string ToUtf8(std::string_view bytes, std::string_view charset) {
  std::string cs = ToLower(charset);
  // UTF-8 passes through unchanged.
  if (cs == "utf-8" || cs == "utf8" || cs.empty()) return std::string(bytes);
  // US-ASCII is a subset of UTF-8.
  if (cs == "us-ascii" || cs == "ascii") return std::string(bytes);
  // ISO-8859-1 / Latin-1: U+0080–U+00FF encoded as two-byte UTF-8 sequences.
  if (cs == "iso-8859-1" || cs == "latin-1" || cs == "iso8859-1") {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
      const auto c = static_cast<unsigned char>(b);
      if (c < 0x80) {
        out += static_cast<char>(c);
      } else {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
      }
    }
    return out;
  }
  // Return raw bytes for unrecognised charsets; caller can use iconv/ICU.
  return std::string(bytes);
}

std::string ToLower(std::string_view sv) {
  std::string s(sv);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return s;
}

std::string_view Trim(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    sv.remove_prefix(1);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
    sv.remove_suffix(1);
  return sv;
}

std::string ExtractParam(std::string_view header_value,
                         std::string_view param_name) {
  std::size_t pos = header_value.find(';');
  while (pos != std::string_view::npos) {
    ++pos;
    while (pos < header_value.size() &&
           std::isspace(static_cast<unsigned char>(header_value[pos])))
      ++pos;

    std::size_t eq = header_value.find('=', pos);
    if (eq == std::string_view::npos) break;

    std::string_view key     = Trim(header_value.substr(pos, eq - pos));
    std::size_t      val_start = eq + 1;
    std::size_t      next_semi = header_value.find(';', val_start);

    std::string_view raw_val =
        (next_semi == std::string_view::npos)
            ? header_value.substr(val_start)
            : header_value.substr(val_start, next_semi - val_start);
    raw_val = Trim(raw_val);

    // Strip surrounding double quotes.
    if (raw_val.size() >= 2 && raw_val.front() == '"' && raw_val.back() == '"')
      raw_val = raw_val.substr(1, raw_val.size() - 2);

    if (ToLower(key) == ToLower(param_name)) return std::string(raw_val);

    pos = next_semi;
  }
  return {};
}

}  // namespace imap::mime
