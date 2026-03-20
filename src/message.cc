#include "imap/message.h"

namespace imap {

// ---------------------------------------------------------------------------
// MimePart convenience helpers
// ---------------------------------------------------------------------------

std::vector<const MimePart*> MimePart::Attachments() const {
  std::vector<const MimePart*> result;
  if (IsAttachment() && parts.empty()) {
    result.push_back(this);
    return result;
  }
  for (const auto& child : parts) {
    auto sub = child.Attachments();
    result.insert(result.end(), sub.begin(), sub.end());
  }
  return result;
}

const MimePart* MimePart::PlainTextPart() const {
  if (content_type == "text/plain" && parts.empty()) return this;
  for (const auto& child : parts) {
    if (const auto* p = child.PlainTextPart()) return p;
  }
  return nullptr;
}

const MimePart* MimePart::HtmlPart() const {
  if (content_type == "text/html" && parts.empty()) return this;
  for (const auto& child : parts) {
    if (const auto* p = child.HtmlPart()) return p;
  }
  return nullptr;
}

}  // namespace imap
