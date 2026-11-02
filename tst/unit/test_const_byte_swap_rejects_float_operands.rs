//@ compile-fail: bswap
#![feature(core_intrinsics)]
const INVALID: f32 = core::intrinsics::bswap(1.0f32);
fn main() { let _ = INVALID; }
