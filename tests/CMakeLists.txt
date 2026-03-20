cmake_minimum_required(VERSION 3.25)

set(TEST_SOURCES
    test_command.cc
    test_error.cc
    test_extensions.cc
    test_message.cc
    test_mime_advanced.cc
    test_parser.cc
    test_session.cc)

add_executable(imap_tests ${TEST_SOURCES})

target_link_libraries(
  imap_tests
  PRIVATE IMAPCore::imapcore
          GTest::gtest
          GTest::gtest_main)

target_compile_options(imap_tests PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

include(GoogleTest)
gtest_discover_tests(imap_tests)
