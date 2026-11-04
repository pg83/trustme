//@ aux-build: delegated_free_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_free_signatures::rotated as selected;

fn main() {
    assert_eq!(selected([151, 157, 163]), [157, 163, 151]);
}
