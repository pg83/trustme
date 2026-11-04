//@ aux-build: delegated_free_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_free_signatures::values as forwarded;

fn combined<T: ExactSizeIterator<Item = u16>>(left: T, right: T) -> usize {
    left.len() + right.len()
}

fn main() {
    assert_eq!(combined(delegated_free_signatures::values(269), forwarded(271)), 4);
}
