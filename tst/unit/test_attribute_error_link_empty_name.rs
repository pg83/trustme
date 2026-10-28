//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Empty name on extern block

#![no_core]

#[link(name = "")]
extern "C" { fn missing(); }
