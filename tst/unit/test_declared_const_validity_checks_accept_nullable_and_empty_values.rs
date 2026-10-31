#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn assert_zero_valid<T>();
#[rustc_intrinsic]
const fn assert_inhabited<T>();
enum Empty {}
const CHECKS: () = {
    assert_zero_valid::<u32>();
    assert_zero_valid::<Option<&u8>>();
    assert_zero_valid::<[u16; 0]>();
    assert_inhabited::<()>();
    assert_inhabited::<Option<Empty>>();
};
fn main() { assert_eq!(CHECKS, ()); }
