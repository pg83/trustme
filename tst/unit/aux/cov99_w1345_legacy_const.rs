#![feature(rustc_attrs)]

#[rustc_legacy_const_generics(0)]
pub fn value<const N: usize>() -> usize {
    N
}
