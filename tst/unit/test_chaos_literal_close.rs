//@ chaos: literal-close@0
//@ compile-fail: Failed to close literal blob
//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only

pub fn answer() -> u32 { 42 }
