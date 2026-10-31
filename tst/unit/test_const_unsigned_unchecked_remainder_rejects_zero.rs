//@ compile-fail: `unchecked_rem` overflowed
#![feature(core_intrinsics)]
const VALUE: u32 = unsafe { std::intrinsics::unchecked_rem(37u32, 0u32) };
fn main() { let _ = VALUE; }
