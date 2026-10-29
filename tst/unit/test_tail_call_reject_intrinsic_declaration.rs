//@ compile-fail: intrinsics cannot be tail-called
#![feature(explicit_tail_calls, core_intrinsics)]
#![allow(incomplete_features)]
fn call(value: u32) -> u32 { become core::intrinsics::bswap(value) }
fn main() {}
