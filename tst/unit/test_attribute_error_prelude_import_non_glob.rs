//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[prelude_import] should be on a glob

#![no_core]

#[prelude_import]
use self::module::Value;
mod module { pub struct Value; }
