//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn fadd_algebraic<T: Copy>(left: T, right: T) -> T;
const fn calculate<T: Copy>(left: T, right: T) -> T { fadd_algebraic(left, right) }
const INVALID: u32 = calculate(13u32, 7u32);
fn main() { let _ = INVALID; }
