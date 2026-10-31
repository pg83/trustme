#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const unsafe fn assume(value: bool);
#[rustc_intrinsic]
const fn is_val_statically_known<T>(value: T) -> bool;
const fn doubled(value: u32) -> u32 {
    unsafe { assume(value < 100); }
    if is_val_statically_known(value) { value + value } else { value * 2 }
}
const ANSWER: u32 = doubled(19);
fn main() { assert_eq!(ANSWER, 38); }
