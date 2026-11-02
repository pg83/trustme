//@ compile-fail: bswap
#![feature(core_intrinsics)]
const INVALID: (u16, u16) = core::intrinsics::bswap((17u16, 31u16));
fn main() { let _ = INVALID; }
