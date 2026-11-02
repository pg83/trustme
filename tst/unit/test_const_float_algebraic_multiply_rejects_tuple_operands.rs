//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn fmul_algebraic<T: Copy>(left: T, right: T) -> T;
const fn calculate<T: Copy>(left: T, right: T) -> T { fmul_algebraic(left, right) }
const INVALID: (u16, u16) = calculate((3, 5), (7, 11));
fn main() { std::hint::black_box(INVALID); }
