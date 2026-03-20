#ifndef IMAP_SESSION_TRANSPORT_H_
#define IMAP_SESSION_TRANSPORT_H_

#include <concepts>
#include <string>
#include <string_view>

#include "imap/response.h"
#include "imap/types.h"

namespace imap {

// ---------------------------------------------------------------------------
// Concept: SessionEventHandler
//
// A type models this concept when it exposes the four I/O event hooks that
// the CRTP session base dispatches through. Concrete session implementations
// (e.g. the callback-based Session, or a future async variant) must satisfy
// this concept.
// ---------------------------------------------------------------------------

template <typename Derived>
concept SessionEventHandler =
    requires(Derived& d, std::string_view sv, SessionState s1, SessionState s2,
             const UntaggedResponse& ur) {
      { d.OnSend(sv) }            -> std::same_as<void>;
      { d.OnStateChange(s1, s2) } -> std::same_as<void>;
      { d.OnUnsolicited(ur) }     -> std::same_as<void>;
      { d.OnError(sv) }           -> std::same_as<void>;
    };

// ---------------------------------------------------------------------------
// Concept: SessionLike
//
// Constrains generic algorithms (test utilities, polling helpers, etc.) over
// any session-compatible object. Covers the minimal subset of the IMAP
// state-machine API most commonly used in generic code.
// ---------------------------------------------------------------------------

template <typename T>
concept SessionLike = requires(T& s, std::string_view sv) {
  { s.Receive(sv) } -> std::same_as<void>;
  { s.State() }     -> std::same_as<SessionState>;
  { s.Logout() }    -> std::convertible_to<std::string>;
};

// ---------------------------------------------------------------------------
// CRTP mixin: SessionBase<Derived>
//
// Provides zero-overhead static dispatch to the four event hooks implemented
// by Derived. The concrete Session class inherits from this and provides
// OnSend / OnStateChange / OnUnsolicited / OnError.
//
// Third-party session variants (async, TLS-wrapping, etc.) should also
// inherit from SessionBase<Derived> and implement the same four hooks.
// ---------------------------------------------------------------------------

template <typename Derived>
class SessionBase {
 protected:
  void DoSend(std::string_view data) {
    static_cast<Derived*>(this)->OnSend(data);
  }

  void DoStateChange(SessionState old_s, SessionState new_s) {
    static_cast<Derived*>(this)->OnStateChange(old_s, new_s);
  }

  void DoUnsolicited(const UntaggedResponse& ur) {
    static_cast<Derived*>(this)->OnUnsolicited(ur);
  }

  void DoError(std::string_view reason) {
    static_cast<Derived*>(this)->OnError(reason);
  }
};

// ---------------------------------------------------------------------------
// Compile-time helper: is T a complete, well-formed session type?
// ---------------------------------------------------------------------------

template <typename T>
inline constexpr bool kIsSessionType =
    SessionLike<T> && SessionEventHandler<T>;

}  // namespace imap

#endif  // IMAP_SESSION_TRANSPORT_H_
