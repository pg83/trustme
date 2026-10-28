//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Empty `modifiers` on extern block

#![no_core]

#[link(name = "unused", modifiers = "")]
extern "C" { fn missing(); }
