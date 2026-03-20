#include "imap/error.h"

#include <gtest/gtest.h>

#include <string>
#include <stdexcept>

namespace imap {
namespace {

// ---------------------------------------------------------------------------
// ErrorCodeMessage
// ---------------------------------------------------------------------------

TEST(ErrorCodeMessageTest, KnownCodesReturnNonEmpty) {
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kNone).empty());
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kProtocolError).empty());
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kAuthFailed).empty());
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kServerNo).empty());
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kMimeParseError).empty());
  EXPECT_FALSE(ErrorCodeMessage(ErrorCode::kExtensionNotSupported).empty());
}

TEST(ErrorCodeMessageTest, NoneReturnsNoError) {
  EXPECT_EQ(ErrorCodeMessage(ErrorCode::kNone), "No error");
}

TEST(ErrorCodeMessageTest, UnknownCodeFallsBack) {
  EXPECT_FALSE(
      ErrorCodeMessage(static_cast<ErrorCode>(200)).empty());
}

// ---------------------------------------------------------------------------
// ImapError
// ---------------------------------------------------------------------------

TEST(ImapErrorTest, WhatContainsMessage) {
  ImapError e(ErrorCode::kAuthFailed);
  EXPECT_NE(std::string(e.what()).find("Authentication failed"),
            std::string::npos);
}

TEST(ImapErrorTest, DetailAppendedToWhat) {
  ImapError e(ErrorCode::kServerNo, "mailbox locked");
  EXPECT_NE(std::string(e.what()).find("mailbox locked"), std::string::npos);
}

TEST(ImapErrorTest, CodeAccessor) {
  ImapError e(ErrorCode::kWrongState, "expected SELECTED");
  EXPECT_EQ(e.Code(), ErrorCode::kWrongState);
}

TEST(ImapErrorTest, DetailAccessor) {
  ImapError e(ErrorCode::kParseError, "unexpected token");
  EXPECT_EQ(e.Detail(), "unexpected token");
}

TEST(ImapErrorTest, IsRuntimeError) {
  EXPECT_NO_THROW({
    try {
      throw ImapError(ErrorCode::kProtocolError, "test");
    } catch (const std::runtime_error& ex) {
      EXPECT_NE(std::string(ex.what()).find("test"), std::string::npos);
    }
  });
}

// ---------------------------------------------------------------------------
// Error value type
// ---------------------------------------------------------------------------

TEST(ErrorStructTest, DefaultIsOk) {
  Error e;
  EXPECT_TRUE(e.Ok());
  EXPECT_EQ(e.code, ErrorCode::kNone);
}

TEST(ErrorStructTest, NoneFactory) {
  Error e = Error::None();
  EXPECT_TRUE(e.Ok());
}

TEST(ErrorStructTest, NonNoneIsNotOk) {
  Error e{ErrorCode::kServerBad, "bad command"};
  EXPECT_FALSE(e.Ok());
  EXPECT_EQ(e.detail, "bad command");
}

TEST(ErrorStructTest, MessageMatchesCode) {
  Error e{ErrorCode::kTagMismatch, {}};
  EXPECT_EQ(e.Message(), ErrorCodeMessage(ErrorCode::kTagMismatch));
}

// ---------------------------------------------------------------------------
// Result<T>
// ---------------------------------------------------------------------------

TEST(ResultTest, OkHasValue) {
  auto r = Result<int>::Ok(42);
  EXPECT_TRUE(r.HasValue());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(r.Value(), 42);
}

TEST(ResultTest, ErrHasNoValue) {
  auto r = Result<int>::Err(ErrorCode::kServerNo, "rejected");
  EXPECT_FALSE(r.HasValue());
  EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultTest, ErrAccessible) {
  auto r = Result<int>::Err(ErrorCode::kAuthFailed, "bad password");
  EXPECT_EQ(r.Err().code, ErrorCode::kAuthFailed);
  EXPECT_EQ(r.Err().detail, "bad password");
}

TEST(ResultTest, OkWithString) {
  auto r = Result<std::string>::Ok("hello");
  EXPECT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), "hello");
}

TEST(ResultTest, ErrWithErrorStruct) {
  Error err{ErrorCode::kMimeEncoding, "unknown charset"};
  auto r = Result<std::string>::Err(err);
  EXPECT_FALSE(r.HasValue());
  EXPECT_EQ(r.Err().code, ErrorCode::kMimeEncoding);
}

// ---------------------------------------------------------------------------
// Result<void>
// ---------------------------------------------------------------------------

TEST(ResultVoidTest, OkHasValue) {
  auto r = Result<void>::Ok();
  EXPECT_TRUE(r.HasValue());
  EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoidTest, ErrHasNoValue) {
  auto r = Result<void>::Err(ErrorCode::kTlsRequired);
  EXPECT_FALSE(r.HasValue());
  EXPECT_EQ(r.Err().code, ErrorCode::kTlsRequired);
}

}  // namespace
}  // namespace imap
