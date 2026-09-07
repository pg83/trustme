#pragma once

inline constexpr const char* RUSTC_TARGET_VERSION = "1.90";
/* The release this toolchain reproduces, as `rustc -V` / `-vV` report it: build scripts
   read the first line (`libc`, `rustversion`, `zerocopy`: `rustc 1.90.0 (...)`) or the
   `release:` field (`autocfg`). */
inline constexpr const char* RUSTC_RELEASE_VERSION = "1.90.0";
inline constexpr const char* RUSTC_RELEASE_COMMIT = "1159e78c4747b02ef996e55082b704c09b970588";
inline constexpr const char* RUSTC_RELEASE_COMMIT_SHORT = "1159e78c4";
inline constexpr const char* RUSTC_RELEASE_DATE = "2025-09-14";
