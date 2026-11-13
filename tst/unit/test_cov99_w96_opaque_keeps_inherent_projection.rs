//@ aux-build: cov99_w96_opaque_inherent_projection.rs
#![feature(fn_delegation, inherent_associated_types)]
#![allow(incomplete_features)]

reuse cov99_w96_opaque_inherent_projection::pair as forwarded;

fn main() {
    let (_, item) = forwarded();
    assert_eq!(item, 96u32);
}
