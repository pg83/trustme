//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Empty `kind` on extern block

#![no_core]

#[link(name = "unused", kind = "")]
extern "C" { fn missing(); }
