//@ compile-fail: floating
#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn frem_algebraic<T: Copy>(left: T, right: T) -> T;
fn main() { let _: &'static u32 = frem_algebraic(&7, &2); }
