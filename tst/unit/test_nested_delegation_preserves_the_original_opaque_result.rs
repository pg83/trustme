//@ aux-build: delegated_free_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_free_signatures::values as first;
reuse crate::first as second;

fn combined<T: ExactSizeIterator<Item = u16>>(left: T, middle: T, right: T) -> usize {
    left.len() + middle.len() + right.len()
}

fn main() {
    assert_eq!(combined(delegated_free_signatures::values(331), first(337), second(347)), 6);
}
