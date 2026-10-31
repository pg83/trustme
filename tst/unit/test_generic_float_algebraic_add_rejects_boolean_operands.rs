//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fadd_algebraic<T: Copy>(left: T, right: T) -> T;
fn calculate<T: Copy>(left: T, right: T) -> T { fadd_algebraic(left, right) }
fn main() { let _: bool = calculate(true, false); }
