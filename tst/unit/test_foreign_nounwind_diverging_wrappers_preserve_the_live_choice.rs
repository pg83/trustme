//@ compile-flags: -Cpanic=abort
unsafe extern "C" { fn abort() -> !; }
#[inline(always)]
fn stop() -> ! { unsafe { abort() } }
#[inline(never)]
fn select(value: u32) -> u32 { if value == 0 { stop() } else { value + 17 } }
fn main() { assert_eq!(select(std::hint::black_box(29)), 46); }
