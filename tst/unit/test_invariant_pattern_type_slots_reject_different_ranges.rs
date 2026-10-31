//@ compile-fail: Value mismatch
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Low = pattern_type!(u16 is 2..=6);
type High = pattern_type!(u16 is 4..=8);
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut value: Low = 4;
    let other: High = 5;
    replace(&mut value, other);
}
