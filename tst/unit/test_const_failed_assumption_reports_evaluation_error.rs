//@ compile-fail: assumption failed during constant evaluation
#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const unsafe fn assume(value: bool);
const fn checked(value: u32) -> u32 { unsafe { assume(value != 0); } value }
const INVALID: u32 = checked(0);
fn main() { let _ = INVALID; }
