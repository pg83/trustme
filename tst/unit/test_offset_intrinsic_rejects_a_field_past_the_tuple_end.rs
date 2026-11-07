//@ compile-fail: error:
#![feature(intrinsics, rustc_attrs)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn offset_of<T>(variant: u32, field: u32) -> usize;

const INVALID: usize = offset_of::<(u8, u16)>(0, 2);

fn main() {
    let _ = INVALID;
}
