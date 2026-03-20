cmake_minimum_required(VERSION 3.25)

project(
  IMAPCore
  VERSION 1.0.0
  DESCRIPTION "C++23 IMAP protocol library"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Compiler warnings (Google style)
# ---------------------------------------------------------------------------
add_library(imap_compile_options INTERFACE)
target_compile_options(
  imap_compile_options
  INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Woverloaded-virtual
      -Wconversion
      -Wsign-conversion
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2>
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>)

# ---------------------------------------------------------------------------
# Library sources
# ---------------------------------------------------------------------------
set(IMAP_SOURCES
    src/message.cc
    src/message_parser.cc
    src/mime/codec.cc
    src/mime/encoded_word.cc
    src/mime/security.cc
    src/command.cc
    src/response.cc
    src/parser.cc
    src/session.cc
    src/extensions.cc)

# Build as shared (.so) by default, allow static override.
option(IMAP_BUILD_SHARED "Build imapcore as a shared library (.so)" ON)
if(IMAP_BUILD_SHARED)
  add_library(imapcore SHARED ${IMAP_SOURCES})
else()
  add_library(imapcore STATIC ${IMAP_SOURCES})
endif()
add_library(IMAPCore::imapcore ALIAS imapcore)

# Set SO version for shared library consumers.
set_target_properties(imapcore PROPERTIES
  VERSION   ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(
  imapcore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                  $<INSTALL_INTERFACE:include>)

target_link_libraries(imapcore PUBLIC imap_compile_options)

# ---------------------------------------------------------------------------
# Optional SPDLOG logging (enabled with -DIMAP_ENABLE_DEBUG=ON)
# ---------------------------------------------------------------------------
option(IMAP_ENABLE_DEBUG "Enable debug logging with spdlog" OFF)

if(IMAP_ENABLE_DEBUG)
  include(FetchContent)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0)
  FetchContent_MakeAvailable(spdlog)
  target_compile_definitions(imapcore PUBLIC DEBUG)
  target_link_libraries(imapcore PUBLIC spdlog::spdlog)
endif()

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
option(IMAP_BUILD_TESTS "Build unit tests" ON)

if(IMAP_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
  # Prevent GoogleTest from overriding our compiler/linker settings on Windows
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules — generates .so, headers, and CMake package config
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(
  TARGETS imapcore imap_compile_options
  EXPORT IMAPCoreTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  EXPORT IMAPCoreTargets
  FILE IMAPCoreTargets.cmake
  NAMESPACE IMAPCore::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IMAPCore)

# Generate IMAPCoreConfig.cmake and IMAPCoreConfigVersion.cmake
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/IMAPCoreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/IMAPCoreConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IMAPCore)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/IMAPCoreConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/IMAPCoreConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/IMAPCoreConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IMAPCore)

# Generate pkg-config .pc file
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imapcore.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/imapcore.pc
  @ONLY)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/imapcore.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
