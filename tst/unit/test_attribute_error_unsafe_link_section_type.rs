//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[unsafe(link_section)] on bad item

#![no_core]

#[unsafe(link_section = ".data.value")]
struct Value;
