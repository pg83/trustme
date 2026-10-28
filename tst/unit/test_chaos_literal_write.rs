//@ chaos: literal-write@0
//@ compile-fail: Failed to write literal blob
//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only

pub static TABLE: [u8; 65536] = [7; 65536];
