//@ compile-fail: zero is not a valid value for a reference
#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn assert_zero_valid<T>();
const INVALID: () = assert_zero_valid::<&u8>();
fn main() { let _ = INVALID; }
