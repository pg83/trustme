//@ compile-fail: bswap
#![feature(core_intrinsics)]
const INVALID: char = core::intrinsics::bswap('x');
fn main() { let _ = INVALID; }
