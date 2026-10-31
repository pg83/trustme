//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fsub_algebraic<T: Copy>(left: T, right: T) -> T;
fn calculate<T: Copy>(left: T, right: T) -> T { fsub_algebraic(left, right) }
fn main() { let _: u32 = calculate(7, 2); }
