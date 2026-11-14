#![feature(adt_const_params, f128)]
#![allow(incomplete_features)]

#[inline(never)]
fn value<const V: f128>() -> f128 {
    V
}

fn main() {
    assert!(value::<1.25f128>() == 1.25f128);
}
