#ifndef IMAP_CALLBACKS_H_
#define IMAP_CALLBACKS_H_

// ---------------------------------------------------------------------------
// Named std::function aliases for all IMAP callback types.
//
// Including this header gives library consumers meaningful type names instead
// of verbose std::function<…> spellings in their own code.
// ---------------------------------------------------------------------------

#include <functional>
#include <string_view>
#include <vector>

#include "imap/response.h"  // TaggedResponse, UntaggedResponse, …
#include "imap/types.h"     // SessionState, FetchedMessage, …

namespace imap {

// ---------------------------------------------------------------------------
// Transport-level hooks (used by SessionCallbacks and SessionBase)
// ---------------------------------------------------------------------------

/// Callback invoked when the session has bytes to write to the transport.
using SendCb = std::function<void(std::string_view)>;

/// Callback invoked with every fully parsed server response (informational).
using ResponseCb = std::function<void(const ParsedResponse&)>;

/// Callback invoked on a session-state transition.
using StateChangeCb = std::function<void(SessionState /*old*/, SessionState /*new*/)>;

/// Callback invoked for unsolicited untagged responses (no pending command).
using UnsolicitedCb = std::function<void(const UntaggedResponse&)>;

/// Callback invoked on a protocol error (BYE, tag mismatch, bad state, …).
using ErrorCb = std::function<void(std::string_view /*reason*/)>;

// ---------------------------------------------------------------------------
// Per-command callbacks
// ---------------------------------------------------------------------------

/// Simple completion callback — no additional data delivered.
using TaggedCb = std::function<void(const TaggedResponse&)>;

/// Untagged-data collector callback used internally by EnqueueCommand.
using UntaggedCb = std::function<void(const UntaggedResponse&)>;

/// AUTHENTICATE: delivers each SASL challenge from the server.
using ContinuationCb = std::function<void(const ContinuationResponse&)>;

/// APPEND: called when the server + continuation is ready for literal bytes.
using LiteralReadyCb = std::function<void()>;

// ---------------------------------------------------------------------------
// Per-command result callbacks (tagged completion + aggregated result)
// ---------------------------------------------------------------------------

/// CAPABILITY completion.
using CapabilityCb =
    std::function<void(const TaggedResponse&, const CapabilityData&)>;

/// SELECT / EXAMINE completion.
using SelectCb =
    std::function<void(const TaggedResponse&, const SelectData&)>;

/// LIST / LSUB completion.
using MailboxListCb =
    std::function<void(const TaggedResponse&, const std::vector<MailboxInfo>&)>;

/// STATUS completion.
using StatusCb =
    std::function<void(const TaggedResponse&, const StatusData&)>;

/// EXPUNGE completion — delivers expunged sequence numbers.
using ExpungeCb =
    std::function<void(const TaggedResponse&, const std::vector<uint32_t>&)>;

/// SEARCH completion.
using SearchCb =
    std::function<void(const TaggedResponse&, const SearchData&)>;

/// FETCH completion.
using FetchCb =
    std::function<void(const TaggedResponse&, const std::vector<FetchedMessage>&)>;

}  // namespace imap

#endif  // IMAP_CALLBACKS_H_
