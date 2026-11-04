//@ aux-build: delegated_free_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_free_signatures::values as forwarded;

fn main() {
    let values = forwarded(223);
    assert_eq!(values.len(), 2);
    assert_eq!(values.clone().sum::<u16>(), 447);
    assert_eq!(values.collect::<Vec<_>>(), [223, 224]);
}
