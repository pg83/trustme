//@ compile-fail: - pattern shapes differ
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Closed = pattern_type!(u32 is 1..=5);
type Open = pattern_type!(u32 is 1..5);
fn same<T>(_: &mut T, _: &mut T) {}
fn main() {
    let mut closed: Closed = 2;
    let mut open: Open = 3;
    same(&mut closed, &mut open);
}
