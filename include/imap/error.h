#ifndef IMAP_ERROR_H_
#define IMAP_ERROR_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace imap {

/**
 * @enum ErrorCode
 * @brief Enumeration of all possible IMAP library error codes.
 */
enum class ErrorCode : uint16_t {
  // Protocol errors
  kNone             = 0,
  kProtocolError    = 1,   ///< Malformed server response or protocol violation
  kTagMismatch      = 2,   ///< Tagged response tag does not match pending command
  kParseError       = 3,   ///< Parser failed to parse a response

  // Authentication errors
  kAuthFailed       = 10,  ///< LOGIN / AUTHENTICATE returned NO
  kTlsRequired      = 11,  ///< STARTTLS required before command
  kNotAuthenticated = 12,  ///< Command requires authenticated state

  // State machine errors
  kWrongState       = 20,  ///< Command not allowed in current session state
  kNoMailboxSelected = 21, ///< Selected-state command issued without SELECT

  // Server-side errors
  kServerNo         = 30,  ///< Server replied NO (command rejected)
  kServerBad        = 31,  ///< Server replied BAD (protocol error on our side)
  kServerBye        = 32,  ///< Server sent BYE (connection closing)

  // MIME errors
  kMimeParseError   = 40,  ///< Cannot parse MIME message
  kMimeEncoding     = 41,  ///< Unknown or unsupported MIME encoding
  kSmimeError       = 42,  ///< S/MIME processing error
  kPgpError         = 43,  ///< PGP/OpenPGP processing error

  // Extension errors
  kExtensionNotSupported = 50, ///< Server does not advertise required capability
  kCondstoreNotSupported = 51,
  kQresyncNotSupported   = 52,

  // Generic
  kInvalidArgument  = 60,
  kUnknown          = 255,
};

/**
 * @brief Returns a human-readable description for an ErrorCode.
 */
inline std::string_view ErrorCodeMessage(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kNone:                    return "No error";
    case ErrorCode::kProtocolError:           return "Protocol error";
    case ErrorCode::kTagMismatch:             return "Tag mismatch";
    case ErrorCode::kParseError:              return "Parse error";
    case ErrorCode::kAuthFailed:              return "Authentication failed";
    case ErrorCode::kTlsRequired:             return "STARTTLS required";
    case ErrorCode::kNotAuthenticated:        return "Not authenticated";
    case ErrorCode::kWrongState:              return "Wrong session state";
    case ErrorCode::kNoMailboxSelected:       return "No mailbox selected";
    case ErrorCode::kServerNo:                return "Server replied NO";
    case ErrorCode::kServerBad:               return "Server replied BAD";
    case ErrorCode::kServerBye:               return "Server sent BYE";
    case ErrorCode::kMimeParseError:          return "MIME parse error";
    case ErrorCode::kMimeEncoding:            return "Unsupported MIME encoding";
    case ErrorCode::kSmimeError:              return "S/MIME error";
    case ErrorCode::kPgpError:                return "PGP error";
    case ErrorCode::kExtensionNotSupported:   return "Extension not supported";
    case ErrorCode::kCondstoreNotSupported:   return "CONDSTORE not supported";
    case ErrorCode::kQresyncNotSupported:     return "QRESYNC not supported";
    case ErrorCode::kInvalidArgument:         return "Invalid argument";
    default:                                  return "Unknown error";
  }
}

/**
 * @class ImapError
 * @brief Exception type thrown by IMAPCore when exceptions are enabled.
 */
class ImapError : public std::runtime_error {
 public:
  explicit ImapError(ErrorCode code, std::string detail = "")
      : std::runtime_error(std::string(ErrorCodeMessage(code)) +
                           (detail.empty() ? "" : ": " + detail)),
        code_(code),
        detail_(std::move(detail)) {}

  /**
   * @brief Returns the machine-readable ErrorCode.
   */
  [[nodiscard]] ErrorCode Code() const noexcept { return code_; }

  /**
   * @brief Returns additional context / detail for the error.
   */
  [[nodiscard]] const std::string& Detail() const noexcept { return detail_; }

 private:
  ErrorCode code_;
  std::string detail_;
};

/**
 * @class Error
 * @brief Lightweight error value carrying an ErrorCode + optional detail string.
 * @details Used as the error type in Result<T>.
 */
struct Error {
  ErrorCode code{ErrorCode::kNone};
  std::string detail;

  [[nodiscard]] bool Ok() const noexcept { return code == ErrorCode::kNone; }
  [[nodiscard]] std::string_view Message() const noexcept { return ErrorCodeMessage(code); }

  static Error None() { return {ErrorCode::kNone, {}}; }
};

/**
 * @class Result
 * @brief Monadic result type that carries either a value T or an Error.
 *
 * @details Use this instead of throwing exceptions where the library is
 * configured in non-throwing mode, or as a return type for fallible operations.
 *
 * @code
 *   Result<std::string> r = SomeOperation();
 *   if (!r) { handle(r.Err()); }
 *   use(r.Value());
 * @endcode
 */
template <typename T>
class Result {
 public:
  /// Construct a successful result.
  static Result Ok(T value) {
    Result r;
    r.storage_ = std::move(value);
    return r;
  }

  /// Construct an error result.
  static Result Err(Error err) {
    Result r;
    r.storage_ = std::move(err);
    return r;
  }

  static Result Err(ErrorCode code, std::string detail = "") {
    return Err(Error{code, std::move(detail)});
  }

  [[nodiscard]] bool HasValue() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  explicit operator bool() const noexcept { return HasValue(); }

  [[nodiscard]] const T& Value() const& { return std::get<T>(storage_); }
  [[nodiscard]] T& Value() & { return std::get<T>(storage_); }
  [[nodiscard]] T Value() && { return std::get<T>(std::move(storage_)); }

  [[nodiscard]] const Error& Err() const& { return std::get<Error>(storage_); }

 private:
  std::variant<T, Error> storage_;
};

/// Specialisation for void (success/failure without value).
template <>
class Result<void> {
 public:
  static Result Ok() {
    Result r;
    r.ok_ = true;
    return r;
  }

  static Result Err(Error err) {
    Result r;
    r.ok_ = false;
    r.err_ = std::move(err);
    return r;
  }

  static Result Err(ErrorCode code, std::string detail = "") {
    return Err(Error{code, std::move(detail)});
  }

  [[nodiscard]] bool HasValue() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }

  [[nodiscard]] const Error& Err() const& { return err_; }

 private:
  bool ok_{false};
  Error err_;
};

}  // namespace imap

#endif  // IMAP_ERROR_H_
