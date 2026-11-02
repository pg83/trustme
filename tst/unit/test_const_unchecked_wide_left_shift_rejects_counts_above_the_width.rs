//@ compile-fail: unchecked_shl
#![feature(core_intrinsics)]

const INVALID: u128 = unsafe { core::intrinsics::unchecked_shl(7u128, 129) };
fn main() { std::hint::black_box(INVALID); }
