//@ compile-fail: intrinsics cannot be tail-called
#![feature(explicit_tail_calls, core_intrinsics)]
#![allow(incomplete_features)]
fn call(flag: bool, left: u32, right: u32) -> u32 { become core::intrinsics::select_unpredictable(flag, left, right) }
fn main() {}
