//@ compile-fail: unchecked_shr
#![feature(core_intrinsics)]

const INVALID: i16 = unsafe { core::intrinsics::unchecked_shr(-9i16, 17) };
fn main() { std::hint::black_box(INVALID); }
