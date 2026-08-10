/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * include/target_version.h
 * - rustc language version fixed by this compiler
 */
#pragma once

inline constexpr const char* RUSTC_TARGET_VERSION = "1.90";

// Compile-time conditions for the remaining version-shaped branches. There is
// no selectable compatibility mode: this compiler implements Rust 1.90.
#define TARGETVER_MOST_1_19 false
#define TARGETVER_MOST_1_29 false
#define TARGETVER_MOST_1_39 false
#define TARGETVER_MOST_1_54 false
#define TARGETVER_MOST_1_74 false
#define TARGETVER_LEAST_1_29 true
#define TARGETVER_LEAST_1_39 true
#define TARGETVER_LEAST_1_54 true
#define TARGETVER_LEAST_1_74 true
#define TARGETVER_LEAST_1_90 true
