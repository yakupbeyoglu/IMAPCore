#include "imap/mime/security.h"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

#include "imap/mime/codec.h"
#include "imap/mime/encoded_word.h"

namespace imap::mime {

ContentDisposition ParseContentDisposition(std::string_view header_value) {
  ContentDisposition cd;
  if (header_value.empty()) return cd;

  // Disposition type is the first token before the first ';'.
  std::size_t semi     = header_value.find(';');
  std::string type_str = ToLower(std::string(
      Trim(semi == std::string_view::npos ? header_value
                                          : header_value.substr(0, semi))));

  if (type_str == "inline")
    cd.type = ContentDispositionType::kInline;
  else if (type_str == "attachment")
    cd.type = ContentDispositionType::kAttachment;

  // RFC 5987 filename is preferred; fall back to plain filename.
  std::string raw_filename = ExtractParam(header_value, "filename");
  if (raw_filename.empty())
    raw_filename = ExtractParam(header_value, "filename*");

  if (!raw_filename.empty())
    cd.filename = DecodeHeaderValue(raw_filename);

  auto get_opt = [&](std::string_view key) -> std::optional<std::string> {
    std::string v = ExtractParam(header_value, key);
    if (v.empty()) return std::nullopt;
    return v;
  };

  cd.creation_date     = get_opt("creation-date");
  cd.modification_date = get_opt("modification-date");
  cd.read_date         = get_opt("read-date");

  std::string size_str = ExtractParam(header_value, "size");
  if (!size_str.empty()) {
    uint64_t sz = 0;
    auto [ptr, ec] = std::from_chars(
        size_str.data(), size_str.data() + size_str.size(), sz);
    if (ec == std::errc{}) cd.size = sz;
  }

  return cd;
}

SecurityInfo DetectSecurity(const MimePart& part) {
  SecurityInfo info;
  const std::string& ct = part.content_type;

  // --- PGP/MIME multipart/signed (RFC 3156) ---
  if (ct == "multipart/signed") {
    auto ct_it = part.headers.find("content-type");
    std::string protocol;
    if (ct_it != part.headers.end())
      protocol = ToLower(ExtractParam(ct_it->second, "protocol"));

    if (protocol == "application/pgp-signature") {
      info.type     = SecurityType::kPgpSigned;
      info.protocol = protocol;
      if (ct_it != part.headers.end())
        info.micalg = ToLower(ExtractParam(ct_it->second, "micalg"));
      if (!part.parts.empty())         info.signed_data = part.parts[0].body;
      if (part.parts.size() >= 2)      info.signature   = part.parts[1].body;
      return info;
    }

    if (protocol == "application/pkcs7-signature" ||
        protocol == "application/x-pkcs7-signature") {
      info.type     = SecurityType::kSmimeSigned;
      info.protocol = protocol;
      if (!part.parts.empty())    info.signed_data = part.parts[0].body;
      if (part.parts.size() >= 2) info.signature   = part.parts[1].body;
      return info;
    }
  }

  // --- PGP/MIME multipart/encrypted (RFC 3156) ---
  if (ct == "multipart/encrypted") {
    auto ct_it = part.headers.find("content-type");
    if (ct_it != part.headers.end()) {
      std::string protocol = ToLower(ExtractParam(ct_it->second, "protocol"));
      if (protocol == "application/pgp-encrypted") {
        info.type     = SecurityType::kPgpEncrypted;
        info.protocol = protocol;
        if (part.parts.size() >= 2) info.signature = part.parts[1].body;
        return info;
      }
    }
  }

  // --- S/MIME opaque-signed / envelopedData ---
  if (ct == "application/pkcs7-mime" || ct == "application/x-pkcs7-mime") {
    info.type      = SecurityType::kSmimeEncrypted;
    info.protocol  = ct;
    info.signature = part.body;
    return info;
  }

  // --- PGP inline armour (detectable from the decoded body text) ---
  const auto& body = part.decoded_body;
  if (body.find("-----BEGIN PGP SIGNED MESSAGE-----") != std::string::npos) {
    info.type = SecurityType::kPgpInlineSigned;
    return info;
  }
  if (body.find("-----BEGIN PGP MESSAGE-----") != std::string::npos) {
    info.type = SecurityType::kPgpInlineEncrypted;
    return info;
  }

  return info;  // SecurityType::kNone
}

}  // namespace imap::mime
