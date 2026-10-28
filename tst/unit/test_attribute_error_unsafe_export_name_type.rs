//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[unsafe(export_name)] on bad item

#![no_core]

#[unsafe(export_name = "value")]
struct Value;
