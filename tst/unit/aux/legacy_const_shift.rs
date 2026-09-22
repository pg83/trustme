#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_legacy_const_generics(1)]
#[inline(always)]
pub fn shift_right<const IMM8: i32>(a: u32) -> u32 {
    if IMM8 >= 32 { 0 } else { a >> IMM8 }
}
