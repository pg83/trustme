//@ aux-build: cov99_w96_inherent_signature.rs
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse cov99_w96_inherent_signature::keep as forwarded;

fn main() {
    assert_eq!(forwarded(96u32), 96u32);
}
