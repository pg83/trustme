//@ compile-fail: attempted to instantiate an uninhabited type
#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn assert_inhabited<T>();
enum Empty {}
const INVALID: () = assert_inhabited::<Empty>();
fn main() { let _ = INVALID; }
