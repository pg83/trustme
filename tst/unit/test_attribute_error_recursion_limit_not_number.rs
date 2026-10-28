//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #![recursion_limit] needs a number

#![no_core]

#![recursion_limit = "many"]
fn function() {}
