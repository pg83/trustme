//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -o '$OUT"blob'

pub static BYTES: [u8; 65536] = [1; 65536];
