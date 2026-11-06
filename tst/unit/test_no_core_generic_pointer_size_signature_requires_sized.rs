//@ crate-type: lib
//@ compile-fail: error
#![feature(no_core, intrinsics, generic_const_exprs)]
#![allow(internal_features, incomplete_features)]
#![no_core]

#[rustc_intrinsic]
const fn size_of<T>() -> usize;

pub fn pointer_bytes<T>() -> [u8; size_of::<*const T>()] {
    loop {}
}
