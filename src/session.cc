#include "imap/session.h"


#ifdef DEBUG
#include <spdlog/spdlog.h>
#endif

#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace imap {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Session::Session(SessionCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

// ---------------------------------------------------------------------------
// Data path
// ---------------------------------------------------------------------------

void Session::Receive(std::string_view data) {
  #ifdef DEBUG
  spdlog::debug("Session::Receive() called with {} bytes", data.size());
  #endif
  parser_.Feed(data);
  while (auto resp = parser_.Next()) {
    #ifdef DEBUG
    spdlog::debug("Session::Dispatch() for parsed response");
    #endif
    Dispatch(*resp);
    if (callbacks_.on_response) callbacks_.on_response(*resp);
  }
}

// ---------------------------------------------------------------------------
// Send helper
// ---------------------------------------------------------------------------

void Session::Send(const std::string& data) {
  #ifdef DEBUG
  spdlog::debug("Session::Send() sending {} bytes", data.size());
  #endif
  DoSend(data);
}

// ---------------------------------------------------------------------------
// State transition
// ---------------------------------------------------------------------------

void Session::Transition(SessionState new_state) {
  if (new_state == state_) return;
  SessionState old = state_;
  state_ = new_state;
  #ifdef DEBUG
  spdlog::debug("Session::Transition() from {} to {}", static_cast<int>(old), static_cast<int>(new_state));
  #endif
  DoStateChange(old, new_state);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void Session::Dispatch(const ParsedResponse& resp) {
  #ifdef DEBUG
  spdlog::debug("Session::Dispatch() type: {}", static_cast<int>(resp.Type()));
  #endif
  if (resp.IsTagged()) {
    DispatchTagged(resp.Tagged());
  } else if (resp.IsUntagged()) {
    DispatchUntagged(resp.Untagged());
  } else if (resp.IsContinuation()) {
    DispatchContinuation(resp.Continuation());
  }
}

void Session::DispatchUntagged(const UntaggedResponse& untagged) {
  std::string kw = untagged.keyword;
  std::transform(kw.begin(), kw.end(), kw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  // Update session-level state from untagged data.
  if (kw == "CAPABILITY") {
    if (auto* cap = std::get_if<CapabilityData>(&untagged.data)) {
      capabilities_ = cap->capabilities;
    }
  } else if (kw == "BYE") {
    Transition(SessionState::kLogout);
  } else if (kw == "OK" || kw == "PREAUTH") {
    if (auto* sr = std::get_if<StatusResponse>(&untagged.data)) {
      // PREAUTH at greeting transitions directly to Authenticated.
      if (sr->status == ResponseStatus::kPreauth) {
        Transition(SessionState::kAuthenticated);
      }
      // Update mailbox state from untagged OK codes.
      if (sr->code.code == ResponseCode::kUidValidity && mailbox_state_) {
        uint32_t val = 0;
        std::from_chars(sr->code.arguments.data(),
                        sr->code.arguments.data() + sr->code.arguments.size(),
                        val);
        mailbox_state_->uid_validity = val;
      } else if (sr->code.code == ResponseCode::kUidNext && mailbox_state_) {
        uint32_t val = 0;
        std::from_chars(sr->code.arguments.data(),
                        sr->code.arguments.data() + sr->code.arguments.size(),
                        val);
        mailbox_state_->uid_next = val;
      } else if (sr->code.code == ResponseCode::kUnseen && mailbox_state_) {
        uint32_t val = 0;
        std::from_chars(sr->code.arguments.data(),
                        sr->code.arguments.data() + sr->code.arguments.size(),
                        val);
        mailbox_state_->unseen = val;
      } else if (sr->code.code == ResponseCode::kPermanentFlags &&
                 mailbox_state_) {
        // Re-parse permanent flags from code arguments.
        // They are in the form "\Seen \Answered \* …"
        // (no parens here, just space-separated).
        std::istringstream iss(sr->code.arguments);
        std::string flag;
        mailbox_state_->permanent_flags.clear();
        while (iss >> flag) mailbox_state_->permanent_flags.push_back(flag);
      } else if (sr->code.code == ResponseCode::kReadOnly && mailbox_state_) {
        mailbox_state_->read_only = true;
      } else if (sr->code.code == ResponseCode::kReadWrite && mailbox_state_) {
        mailbox_state_->read_only = false;
      }
    }
  }

  // EXISTS / RECENT / EXPUNGE — update mailbox state.
  if (untagged.number.has_value()) {
    if (kw == "EXISTS" && mailbox_state_) {
      mailbox_state_->exists = *untagged.number;
    } else if (kw == "RECENT" && mailbox_state_) {
      mailbox_state_->recent = *untagged.number;
    }
  }

  // FLAGS response during SELECT/EXAMINE.
  if (kw == "FLAGS") {
    if (auto* fd = std::get_if<FlagsData>(&untagged.data)) {
      if (mailbox_state_) mailbox_state_->flags = fd->flags;
    }
  }

  // Feed to the front pending command's per-untagged callback.
  if (!pending_.empty() && pending_.front().on_untagged) {
    pending_.front().on_untagged(untagged);
  }

  // Fire unsolicited if no pending command owns this.
  if (pending_.empty()) {
    DoUnsolicited(untagged);
  }
}

void Session::DispatchTagged(const TaggedResponse& tagged) {
  if (pending_.empty()) return;

  PendingCommand& front = pending_.front();
  if (front.tag != tagged.tag) {
    // Tag mismatch — protocol error.
    DoError("Tag mismatch in tagged response");
    return;
  }

  // Post-command state transitions.
  switch (front.type) {
    case CommandType::kLogin:
    case CommandType::kAuthenticate:
      if (tagged.status.status == ResponseStatus::kOk)
        Transition(SessionState::kAuthenticated);
      break;
    case CommandType::kSelect:
    case CommandType::kExamine:
      if (tagged.status.status == ResponseStatus::kOk)
        Transition(SessionState::kSelected);
      else
        mailbox_state_.reset();
      break;
    case CommandType::kClose:
      if (tagged.status.status == ResponseStatus::kOk) {
        selected_mailbox_.clear();
        mailbox_state_.reset();
        Transition(SessionState::kAuthenticated);
      }
      break;
    case CommandType::kLogout:
      Transition(SessionState::kLogout);
      break;
    case CommandType::kIdle:
      is_idle_ = false;
      break;
    default:
      break;
  }

  if (front.on_complete) front.on_complete(tagged);
  pending_.pop();
}

void Session::DispatchContinuation(const ContinuationResponse& cont) {
  // APPEND literal ready.
  if (append_literal_cb_) {
    auto cb = std::move(append_literal_cb_);
    append_literal_cb_ = nullptr;
    cb();
    return;
  }
  // AUTHENTICATE challenge.
  if (auth_challenge_cb_) {
    auth_challenge_cb_(cont);
    return;
  }
  // IDLE continuation (server accepted, now we are in IDLE mode).
  if (!pending_.empty() &&
      pending_.front().type == CommandType::kIdle) {
    is_idle_ = true;
  }
}

// ---------------------------------------------------------------------------
// Enqueue helper
// ---------------------------------------------------------------------------

std::string Session::EnqueueCommand(
    CommandType type, const std::string& command_line,
    std::function<void(const TaggedResponse&)> on_complete,
    std::function<void(const UntaggedResponse&)> on_untagged) {
  PendingCommand cmd;
  cmd.tag         = builder_.LastTag();
  cmd.type        = type;
  cmd.on_complete = std::move(on_complete);
  cmd.on_untagged = std::move(on_untagged);
  pending_.push(std::move(cmd));
  Send(command_line);
  return builder_.LastTag();
}

// ---------------------------------------------------------------------------
// State guards (RFC 3501 §3)
// ---------------------------------------------------------------------------

bool Session::GuardState(SessionState min_required, std::string_view cmd) {
  if (state_ == SessionState::kLogout) {
    DoError(std::string(cmd) + ": session is in Logout state");
    return false;
  }
  if (state_ < min_required) {
    DoError(std::string(cmd) + ": command not allowed in current session state");
    return false;
  }
  return true;
}

bool Session::GuardNotAuthenticated(std::string_view cmd) {
  if (state_ != SessionState::kNotAuthenticated) {
    DoError(std::string(cmd) + ": command only allowed in NotAuthenticated state");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Public command methods
// ---------------------------------------------------------------------------

std::string Session::Capability(
    std::function<void(const TaggedResponse&, const CapabilityData&)>
        on_complete) {
  auto untagged_col = std::make_shared<CapabilityData>();
  return EnqueueCommand(
      CommandType::kCapability, builder_.Capability(),
      [uc = untagged_col, cb = std::move(on_complete)](
          const TaggedResponse& tr) {
        if (cb) cb(tr, *uc);
      },
      [uc = untagged_col](const UntaggedResponse& ur) {
        if (auto* cap = std::get_if<CapabilityData>(&ur.data)) {
          *uc = *cap;
        }
      });
}

std::string Session::Noop(
    std::function<void(const TaggedResponse&)> on_complete) {
  return EnqueueCommand(CommandType::kNoop, builder_.Noop(),
                        std::move(on_complete));
}

std::string Session::Logout(
    std::function<void(const TaggedResponse&)> on_complete) {
  return EnqueueCommand(CommandType::kLogout, builder_.Logout(),
                        std::move(on_complete));
}

std::string Session::StartTls(
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardNotAuthenticated("STARTTLS")) return {};
  return EnqueueCommand(CommandType::kStartTls, builder_.StartTls(),
                        std::move(on_complete));
}

std::string Session::Login(
    std::string_view userid, std::string_view password,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardNotAuthenticated("LOGIN")) return {};
  return EnqueueCommand(CommandType::kLogin,
                        builder_.Login(userid, password),
                        std::move(on_complete));
}

std::string Session::Authenticate(
    std::string_view mechanism,
    std::function<void(const ContinuationResponse&)> on_challenge,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardNotAuthenticated("AUTHENTICATE")) return {};
  auth_challenge_cb_ = std::move(on_challenge);
  return EnqueueCommand(CommandType::kAuthenticate,
                        builder_.Authenticate(mechanism),
                        std::move(on_complete));
}

std::string Session::Select(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&, const SelectData&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "SELECT")) return {};
  mailbox_state_ = SelectData{};
  selected_mailbox_ = std::string(mailbox);

  auto sd = std::make_shared<SelectData>();
  return EnqueueCommand(
      CommandType::kSelect, builder_.Select(mailbox),
      [sd, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *sd);
      },
      [this, sd](const UntaggedResponse& ur) {
        std::string kw = ur.keyword;
        std::transform(kw.begin(), kw.end(), kw.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (kw == "EXISTS" && ur.number) sd->exists = *ur.number;
        if (kw == "RECENT" && ur.number) sd->recent = *ur.number;
        if (kw == "FLAGS") {
          if (auto* fd = std::get_if<FlagsData>(&ur.data)) sd->flags = fd->flags;
        }
        // Sync back to session.
        if (mailbox_state_) *mailbox_state_ = *sd;
      });
}

std::string Session::Examine(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&, const SelectData&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "EXAMINE")) return {};
  mailbox_state_ = SelectData{};
  selected_mailbox_ = std::string(mailbox);

  auto sd = std::make_shared<SelectData>();
  return EnqueueCommand(
      CommandType::kExamine, builder_.Examine(mailbox),
      [sd, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *sd);
      },
      [this, sd](const UntaggedResponse& ur) {
        std::string kw = ur.keyword;
        std::transform(kw.begin(), kw.end(), kw.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (kw == "EXISTS" && ur.number) sd->exists = *ur.number;
        if (kw == "RECENT" && ur.number) sd->recent = *ur.number;
        if (kw == "FLAGS") {
          if (auto* fd = std::get_if<FlagsData>(&ur.data)) sd->flags = fd->flags;
        }
        if (mailbox_state_) *mailbox_state_ = *sd;
      });
}

std::string Session::Create(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "CREATE")) return {};
  return EnqueueCommand(CommandType::kCreate, builder_.Create(mailbox),
                        std::move(on_complete));
}

std::string Session::Delete(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "DELETE")) return {};
  return EnqueueCommand(CommandType::kDelete, builder_.Delete(mailbox),
                        std::move(on_complete));
}

std::string Session::Rename(
    std::string_view existing_name, std::string_view new_name,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "RENAME")) return {};
  return EnqueueCommand(CommandType::kRename,
                        builder_.Rename(existing_name, new_name),
                        std::move(on_complete));
}

std::string Session::Subscribe(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "SUBSCRIBE")) return {};
  return EnqueueCommand(CommandType::kSubscribe, builder_.Subscribe(mailbox),
                        std::move(on_complete));
}

std::string Session::Unsubscribe(
    std::string_view mailbox,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "UNSUBSCRIBE")) return {};
  return EnqueueCommand(CommandType::kUnsubscribe,
                        builder_.Unsubscribe(mailbox), std::move(on_complete));
}

std::string Session::List(
    std::string_view reference, std::string_view pattern,
    std::function<void(const TaggedResponse&, const std::vector<MailboxInfo>&)>
        on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "LIST")) return {};
  auto boxes = std::make_shared<std::vector<MailboxInfo>>();
  return EnqueueCommand(
      CommandType::kList, builder_.List(reference, pattern),
      [boxes, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *boxes);
      },
      [boxes](const UntaggedResponse& ur) {
        if (auto* ld = std::get_if<ListData>(&ur.data)) {
          for (auto& m : ld->mailboxes) boxes->push_back(m);
        }
      });
}

std::string Session::Lsub(
    std::string_view reference, std::string_view pattern,
    std::function<void(const TaggedResponse&, const std::vector<MailboxInfo>&)>
        on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "LSUB")) return {};
  auto boxes = std::make_shared<std::vector<MailboxInfo>>();
  return EnqueueCommand(
      CommandType::kLsub, builder_.Lsub(reference, pattern),
      [boxes, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *boxes);
      },
      [boxes](const UntaggedResponse& ur) {
        if (auto* ld = std::get_if<ListData>(&ur.data)) {
          for (auto& m : ld->mailboxes) boxes->push_back(m);
        }
      });
}

std::string Session::Status(
    std::string_view mailbox, const std::vector<StatusItem>& items,
    std::function<void(const TaggedResponse&, const StatusData&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "STATUS")) return {};
  auto sd = std::make_shared<StatusData>();
  return EnqueueCommand(
      CommandType::kStatus, builder_.Status(mailbox, items),
      [sd, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *sd);
      },
      [sd](const UntaggedResponse& ur) {
        if (auto* data = std::get_if<StatusData>(&ur.data)) *sd = *data;
      });
}

std::string Session::Append(
    std::string_view mailbox, uint64_t literal_size,
    std::optional<MessageFlags> flags,
    std::optional<std::string_view> date_time,
    std::function<void()> on_literal_ready,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kAuthenticated, "APPEND")) return {};
  append_literal_cb_ = std::move(on_literal_ready);
  return EnqueueCommand(
      CommandType::kAppend,
      builder_.Append(mailbox, literal_size, flags, date_time),
      std::move(on_complete));
}

std::string Session::Check(
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "CHECK")) return {};
  return EnqueueCommand(CommandType::kCheck, builder_.Check(),
                        std::move(on_complete));
}

std::string Session::Close(
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "CLOSE")) return {};
  return EnqueueCommand(CommandType::kClose, builder_.Close(),
                        std::move(on_complete));
}

std::string Session::Expunge(
    std::function<void(const TaggedResponse&, const std::vector<uint32_t>&)>
        on_complete) {
  if (!GuardState(SessionState::kSelected, "EXPUNGE")) return {};
  auto nums = std::make_shared<std::vector<uint32_t>>();
  return EnqueueCommand(
      CommandType::kExpunge, builder_.Expunge(),
      [nums, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *nums);
      },
      [nums](const UntaggedResponse& ur) {
        if (auto* c = std::get_if<CountData>(&ur.data)) {
          std::string kw = ur.keyword;
          std::transform(kw.begin(), kw.end(), kw.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
          if (kw == "EXPUNGE") nums->push_back(c->count);
        }
      });
}

std::string Session::Search(
    const std::vector<std::string>& criteria, bool uid,
    std::function<void(const TaggedResponse&, const SearchData&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "SEARCH")) return {};
  auto sd = std::make_shared<SearchData>();
  return EnqueueCommand(
      CommandType::kSearch, builder_.Search(criteria, uid),
      [sd, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *sd);
      },
      [sd](const UntaggedResponse& ur) {
        if (auto* s = std::get_if<SearchData>(&ur.data)) *sd = *s;
      });
}

std::string Session::Fetch(
    std::string_view sequence_set, FetchItems items,
    const std::vector<BodySection>& sections, bool uid,
    std::function<void(const TaggedResponse&, const std::vector<FetchedMessage>&)>
        on_complete) {
  if (!GuardState(SessionState::kSelected, "FETCH")) return {};
  auto msgs = std::make_shared<std::vector<FetchedMessage>>();
  return EnqueueCommand(
      CommandType::kFetch,
      builder_.Fetch(sequence_set, items, sections, uid),
      [msgs, cb = std::move(on_complete)](const TaggedResponse& tr) {
        if (cb) cb(tr, *msgs);
      },
      [msgs](const UntaggedResponse& ur) {
        if (auto* fd = std::get_if<FetchData>(&ur.data)) {
          for (auto& m : fd->messages) msgs->push_back(m);
        }
      });
}

std::string Session::Store(
    std::string_view sequence_set, std::string_view operation,
    MessageFlags flags, bool uid,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "STORE")) return {};
  return EnqueueCommand(CommandType::kStore,
                        builder_.Store(sequence_set, operation, flags, uid),
                        std::move(on_complete));
}

std::string Session::Copy(
    std::string_view sequence_set, std::string_view mailbox, bool uid,
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "COPY")) return {};
  return EnqueueCommand(CommandType::kCopy,
                        builder_.Copy(sequence_set, mailbox, uid),
                        std::move(on_complete));
}

std::string Session::Idle(
    std::function<void(const TaggedResponse&)> on_complete) {
  if (!GuardState(SessionState::kSelected, "IDLE")) return {};
  return EnqueueCommand(CommandType::kIdle, builder_.Idle(),
                        std::move(on_complete));
}

void Session::IdleDone() {
  if (!is_idle_) return;
  Send(CommandBuilder::Done());
}

bool Session::HasCapability(std::string_view cap) const {
  return std::find(capabilities_.begin(), capabilities_.end(), cap) !=
         capabilities_.end();
}

// ---------------------------------------------------------------------------
// CRTP event hook implementations
// ---------------------------------------------------------------------------

void Session::OnSend(std::string_view data) {
  if (callbacks_.on_send) callbacks_.on_send(data);
}

void Session::OnStateChange(SessionState old_state, SessionState new_state) {
  if (callbacks_.on_state_change) callbacks_.on_state_change(old_state, new_state);
}

void Session::OnUnsolicited(const UntaggedResponse& ur) {
  if (callbacks_.on_unsolicited) callbacks_.on_unsolicited(ur);
}

void Session::OnError(std::string_view reason) {
  if (callbacks_.on_error) callbacks_.on_error(reason);
}

}  // namespace imap
