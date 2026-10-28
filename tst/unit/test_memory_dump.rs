//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand --env-set TRUSTME_DUMPMEM=1

#![no_core]

pub fn answer() -> u32 { 42 }
