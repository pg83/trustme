//@ compile-fail: three_way_compare
#![feature(core_intrinsics)]
const INVALID: core::cmp::Ordering = core::intrinsics::three_way_compare((1u8, 2u8), (3u8, 4u8));
fn main() { let _ = INVALID; }
