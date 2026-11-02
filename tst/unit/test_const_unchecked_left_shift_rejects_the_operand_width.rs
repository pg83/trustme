//@ compile-fail: unchecked_shl
#![feature(core_intrinsics)]

const INVALID: u8 = unsafe { core::intrinsics::unchecked_shl(3u8, 8) };
fn main() { std::hint::black_box(INVALID); }
