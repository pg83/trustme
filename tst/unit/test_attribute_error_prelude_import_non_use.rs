//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Invalid use of #[prelude_import] on non-use

#![no_core]

#[prelude_import]
struct Value;
