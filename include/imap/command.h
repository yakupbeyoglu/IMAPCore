#ifndef IMAP_COMMAND_H_
#define IMAP_COMMAND_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "imap/types.h"

namespace imap {


/**
 * @class CommandBuilder
 * @brief Produces properly formatted IMAP command strings (with tag) that can be
 * written verbatim to any transport layer.
 *
 * Example usage:
 * @code
 *   CommandBuilder builder;
 *   std::string cmd = builder.Login("user@example.com", "s3cr3t");
 *   // -> "A001 LOGIN user@example.com s3cr3t\r\n"
 * @endcode
 */
class CommandBuilder {
 public:
  explicit CommandBuilder(std::string_view tag_prefix = "A");

  /**
   * @brief Resets the tag counter.
   */
  void Reset();

  /**
   * @brief Returns the last tag that was generated.
   * @return The last generated tag as a string.
   */
  [[nodiscard]] std::string LastTag() const { return last_tag_; }

  /// @name Any-state commands
  /// @{
  std::string Capability();
  std::string Noop();
  std::string Logout();

  /// @}
  /// @name Not-authenticated-state commands
  /// @{
  std::string StartTls();

  /**
   * @brief AUTHENTICATE (RFC 3501 §6.2.2) — sends just the initial command line.
   * @details Continuation challenge/response handling is done at the session layer.
   */
  std::string Authenticate(std::string_view mechanism);

  /**
   * @brief LOGIN (RFC 3501 §6.2.3)
   * @details userid and password are quoted-string escaped automatically.
   */
  std::string Login(std::string_view userid, std::string_view password);

  // --- Authenticated-state commands ---
  std::string Select(std::string_view mailbox);
  std::string Examine(std::string_view mailbox);
  std::string Create(std::string_view mailbox);
  std::string Delete(std::string_view mailbox);
  std::string Rename(std::string_view existing_name,
                     std::string_view new_name);
  std::string Subscribe(std::string_view mailbox);
  std::string Unsubscribe(std::string_view mailbox);

  /**
   * @brief LIST / LSUB (reference + mailbox pattern, e.g. "" and "*")
   */
  std::string List(std::string_view reference, std::string_view mailbox_pattern);
  std::string Lsub(std::string_view reference, std::string_view mailbox_pattern);

  /**
   * @brief STATUS command.
   */
  std::string Status(std::string_view mailbox,
                     const std::vector<StatusItem>& items);

  /**
   * @brief APPEND — literal size is provided; the caller must transmit the literal
   * after receiving the server's continuation response.
   */
  std::string Append(std::string_view mailbox, uint64_t literal_size,
                     std::optional<MessageFlags> flags = std::nullopt,
                     std::optional<std::string_view> date_time = std::nullopt);

  /// @}
  /// @name Selected-state commands
  /// @{
  std::string Check();
  std::string Close();
  std::string Expunge();

  // SEARCH
  std::string Search(const std::vector<std::string>& criteria,
                     bool uid = false);

  /**
   * @brief FETCH — sequence_set is already formatted (e.g. "1:*", "1,3,5")
   */
  std::string Fetch(std::string_view sequence_set, FetchItems items,
                    const std::vector<BodySection>& sections = {},
                    bool uid = false);

  /**
   * @brief STORE command.
   * @param operation Operation type: "+FLAGS", "-FLAGS", "FLAGS" (or .SILENT variants)
   */
  std::string Store(std::string_view sequence_set, std::string_view operation,
                    MessageFlags flags, bool uid = false);

  /**
   * @brief COPY command.
   */
  std::string Copy(std::string_view sequence_set, std::string_view mailbox,
                   bool uid = false);

  /**
   * @brief IDLE (RFC 2177) — sends "TAG IDLE\r\n"
   */
  std::string Idle();
  /**
   * @brief DONE — terminates IDLE (no tag)
   */
  static std::string Done();

 private:
  std::string NextTag();
  static std::string QuoteString(std::string_view s);
  static std::string FormatMailbox(std::string_view mailbox);
  static std::string FormatFlags(MessageFlags flags);
  static std::string FormatFetchItems(FetchItems items,
                                      const std::vector<BodySection>& sections);
  static std::string FormatStatusItems(const std::vector<StatusItem>& items);

  std::string tag_prefix_;
  uint64_t tag_counter_{0};
  std::string last_tag_;
};

/**
 * @class SequenceSetBuilder
 * @brief Helper to construct IMAP sequence sets.
 */
class SequenceSetBuilder {
 public:
  SequenceSetBuilder& Add(uint32_t number);
  SequenceSetBuilder& AddRange(uint32_t from, uint32_t to);
  /**
   * @brief Appends a wildcard ('*') to the sequence set.
   */
  SequenceSetBuilder& AddWildcard();

  [[nodiscard]] std::string Build() const;
  void Clear();

 private:
  std::vector<std::string> parts_;
};

}  // namespace imap

#endif  // IMAP_COMMAND_H_
