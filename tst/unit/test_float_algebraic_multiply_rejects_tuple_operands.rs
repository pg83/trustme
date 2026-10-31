//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fmul_algebraic<T: Copy>(left: T, right: T) -> T;
fn main() { let _: (u8, u8) = fmul_algebraic((1, 2), (3, 4)); }
