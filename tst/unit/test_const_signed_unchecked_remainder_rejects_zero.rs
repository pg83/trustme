//@ compile-fail: `unchecked_rem` overflowed
#![feature(core_intrinsics)]
const VALUE: i32 = unsafe { std::intrinsics::unchecked_rem(-37i32, 0i32) };
fn main() { let _ = VALUE; }
