//@ compile-fail: `unchecked_div` overflowed
#![feature(core_intrinsics)]
const VALUE: u32 = unsafe { std::intrinsics::unchecked_div(37u32, 0u32) };
fn main() { let _ = VALUE; }
