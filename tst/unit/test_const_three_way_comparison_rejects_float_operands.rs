//@ compile-fail: three_way_compare
#![feature(core_intrinsics)]
const INVALID: core::cmp::Ordering = core::intrinsics::three_way_compare(1.0f32, 2.0f32);
fn main() { let _ = INVALID; }
