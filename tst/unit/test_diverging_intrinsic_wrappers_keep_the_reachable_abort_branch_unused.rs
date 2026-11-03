//@ compile-flags: -Cpanic=abort
#![feature(core_intrinsics)]
#[inline(always)]
fn stop() -> ! { core::intrinsics::abort() }
#[inline(never)]
fn select(value: u32) -> u32 { if value == 0 { stop() } else { value + 19 } }
fn main() { assert_eq!(select(std::hint::black_box(23)), 42); }
