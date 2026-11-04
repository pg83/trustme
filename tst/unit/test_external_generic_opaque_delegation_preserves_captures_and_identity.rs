//@ aux-build: delegated_generic_opaque.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_generic_opaque::values as forwarded;

fn combined<T: ExactSizeIterator<Item = u16>>(left: T, right: T) -> usize {
    left.len() + right.len()
}

fn main() {
    let first = delegated_generic_opaque::values([277u16, 281, 283]);
    let second = forwarded([293u16, 307, 311]);
    assert_eq!(second.clone().sum::<u16>(), 911);
    assert_eq!(combined(first, second), 6);
}
