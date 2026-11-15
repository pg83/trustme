//@ compile-fail: intrinsic
#![feature(intrinsics)]

#[rustc_intrinsic]
unsafe fn raw_eq<T>(left: T, right: T) -> bool;

fn item() {}

fn main() {
    unsafe { raw_eq(item, item) };
}
