//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: No name in `#[link]`

#![no_core]

#[link(kind = "static")]
extern "C" { fn missing(); }
