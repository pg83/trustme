//@ compile-fail: `unchecked_div` overflowed
#![feature(core_intrinsics)]
const VALUE: i32 = unsafe { std::intrinsics::unchecked_div(-37i32, 0i32) };
fn main() { let _ = VALUE; }
