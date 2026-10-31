//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fdiv_algebraic<T: Copy>(left: T, right: T) -> T;
fn main() { let _: char = fdiv_algebraic('x', 'y'); }
