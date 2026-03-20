#ifndef IMAP_SESSION_H_
#define IMAP_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "imap/callbacks.h"
#include "imap/command.h"
#include "imap/parser.h"
#include "imap/response.h"
#include "imap/session_transport.h"
#include "imap/types.h"

namespace imap {

/**
 * @struct PendingCommand
 * @brief Tracks an in-flight command waiting for a tagged response.
 */
struct PendingCommand {
  std::string tag;
  CommandType type{CommandType::kNoop};
  // Called when the tagged response arrives (OK / NO / BAD).
  TaggedCb on_complete;
  // Called for each untagged response collected while this command is active.
  UntaggedCb on_untagged;
};

/**
 * @struct SessionCallbacks
 * @brief The caller provides these callbacks so the Session can be decoupled from
 * any concrete socket/transport layer.
 */
struct SessionCallbacks {
  // Called whenever the Session has bytes to send to the server.
  SendCb on_send;

  // Called when a complete server response has been parsed (informational).
  ResponseCb on_response;

  // Called when the session state changes.
  StateChangeCb on_state_change;

  // Called when an unsolicited (untagged) response arrives that is not
  // associated with any pending command (e.g. EXISTS, EXPUNGE, FLAGS).
  UnsolicitedCb on_unsolicited;

  // Called on a protocol error (BYE, parse failure, etc.)
  ErrorCb on_error;
};

/**
 * @class Session
 * @brief Implements the IMAP client-side state machine (RFC 3501 §3).
 * It builds and serialises commands, feeds incoming bytes to the parser, and
 * dispatches responses to the appropriate callbacks.
 *
 * @note Thread safety: NOT thread-safe.  Serialise calls from a single thread or
 * use external locking.
 */
class Session : public SessionBase<Session> {
 public:
  explicit Session(SessionCallbacks callbacks);

  // --- Data path ---

    /**
     * @brief Feed raw bytes received from the server.
     * @param data The data received from the server.
     */
    void Receive(std::string_view data);

    /**
     * @name Command API
     * @brief Each method enqueues a command. `on_complete` is invoked when the server sends the tagged response. `on_untagged` (optional) is called for each untagged data response while this command is in-flight.
     * @return The tag string assigned to this command.
     */

  std::string Capability(CapabilityCb on_complete = nullptr);

  std::string Noop(TaggedCb on_complete = nullptr);

  std::string Logout(TaggedCb on_complete = nullptr);

  std::string StartTls(TaggedCb on_complete = nullptr);

  std::string Login(
      std::string_view userid, std::string_view password,
      TaggedCb on_complete = nullptr);

  std::string Authenticate(
      std::string_view mechanism,
      ContinuationCb on_challenge,
      TaggedCb on_complete = nullptr);

  std::string Select(
      std::string_view mailbox,
      SelectCb on_complete = nullptr);

  std::string Examine(
      std::string_view mailbox,
      SelectCb on_complete = nullptr);

  std::string Create(
      std::string_view mailbox,
      TaggedCb on_complete = nullptr);

  std::string Delete(
      std::string_view mailbox,
      TaggedCb on_complete = nullptr);

  std::string Rename(
      std::string_view existing_name, std::string_view new_name,
      TaggedCb on_complete = nullptr);

  std::string Subscribe(
      std::string_view mailbox,
      TaggedCb on_complete = nullptr);

  std::string Unsubscribe(
      std::string_view mailbox,
      TaggedCb on_complete = nullptr);

  std::string List(
      std::string_view reference, std::string_view pattern,
      MailboxListCb on_complete = nullptr);

  std::string Lsub(
      std::string_view reference, std::string_view pattern,
      MailboxListCb on_complete = nullptr);

  std::string Status(
      std::string_view mailbox, const std::vector<StatusItem>& items,
      StatusCb on_complete = nullptr);

  // APPEND — caller must transmit the literal bytes separately after the
  // continuation response is delivered via `on_literal_ready`.
  std::string Append(
      std::string_view mailbox, uint64_t literal_size,
      std::optional<MessageFlags> flags,
      std::optional<std::string_view> date_time,
      LiteralReadyCb on_literal_ready,
      TaggedCb on_complete = nullptr);

  std::string Check(TaggedCb on_complete = nullptr);

  std::string Close(TaggedCb on_complete = nullptr);

  std::string Expunge(ExpungeCb on_complete = nullptr);

  std::string Search(
      const std::vector<std::string>& criteria, bool uid = false,
      SearchCb on_complete = nullptr);

  std::string Fetch(
      std::string_view sequence_set, FetchItems items,
      const std::vector<BodySection>& sections = {},
      bool uid = false,
      FetchCb on_complete = nullptr);

  std::string Store(
      std::string_view sequence_set, std::string_view operation,
      MessageFlags flags, bool uid = false,
      TaggedCb on_complete = nullptr);

  std::string Copy(
      std::string_view sequence_set, std::string_view mailbox,
      bool uid = false,
      TaggedCb on_complete = nullptr);

  // IDLE support (RFC 2177).
  std::string Idle(TaggedCb on_complete = nullptr);

  // Sends DONE to terminate IDLE.
  void IdleDone();

  // --- CRTP event hooks (called by SessionBase<Session>::DoXxx) ---
  void OnSend(std::string_view data);
  void OnStateChange(SessionState old_state, SessionState new_state);
  void OnUnsolicited(const UntaggedResponse& ur);
  void OnError(std::string_view reason);

  // --- Accessors ---
  [[nodiscard]] SessionState State() const { return state_; }
  [[nodiscard]] const std::string& SelectedMailbox() const {
    return selected_mailbox_;
  }
  [[nodiscard]] const std::vector<std::string>& Capabilities() const {
    return capabilities_;
  }
  [[nodiscard]] bool HasCapability(std::string_view cap) const;
  [[nodiscard]] bool IsIdle() const { return is_idle_; }

  // The last successfully completed SELECT/EXAMINE data snapshot.
  [[nodiscard]] const std::optional<SelectData>& MailboxState() const {
    return mailbox_state_;
  }

 private:
  void Send(const std::string& data);
  void Transition(SessionState new_state);

  // State guards — call DoError and return false if the precondition is not met.
  // RFC 3501 §3: each command is only valid in specific session states.
  bool GuardState(SessionState min_required, std::string_view cmd);
  bool GuardNotAuthenticated(std::string_view cmd);

  // Dispatches a fully parsed response.
  void Dispatch(const ParsedResponse& resp);
  void DispatchUntagged(const UntaggedResponse& untagged);
  void DispatchTagged(const TaggedResponse& tagged);
  void DispatchContinuation(const ContinuationResponse& cont);

  // Enqueues and immediately sends a command line.
  std::string EnqueueCommand(
      CommandType type,
      const std::string& command_line,
      TaggedCb on_complete,
      UntaggedCb on_untagged = nullptr);

  SessionCallbacks callbacks_;
  CommandBuilder builder_;
  ResponseParser parser_;

  SessionState state_{SessionState::kNotAuthenticated};
  std::string selected_mailbox_;
  std::vector<std::string> capabilities_;
  std::optional<SelectData> mailbox_state_;
  bool is_idle_{false};

  // Pipeline: commands waiting for their tagged response.
  std::queue<PendingCommand> pending_;

  // APPEND literal callback.
  LiteralReadyCb append_literal_cb_;

  // AUTHENTICATE challenge callback.
  ContinuationCb auth_challenge_cb_;
};

// Verify at compile time that Session satisfies both concepts.
static_assert(kIsSessionType<Session>,
              "Session must satisfy SessionLike and SessionEventHandler");

}  // namespace imap

#endif  // IMAP_SESSION_H_
