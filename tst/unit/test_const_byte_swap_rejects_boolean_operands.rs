//@ compile-fail: bswap
#![feature(core_intrinsics)]
const INVALID: bool = core::intrinsics::bswap(true);
fn main() { let _ = INVALID; }
