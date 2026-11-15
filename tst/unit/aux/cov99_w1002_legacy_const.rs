#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_legacy_const_generics(0)]
#[inline(never)]
pub fn value<const N: usize>() -> usize {
    N
}
