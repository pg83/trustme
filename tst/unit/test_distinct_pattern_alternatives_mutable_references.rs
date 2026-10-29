//@ compile-fail: - pattern alternative counts differ
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Single = pattern_type!(u32 is 1..=5);
fn same<T>(_: &mut T, _: &mut T) {}
fn main() {
    let mut single: Single = 2;
    let mut either: pattern_type!(u32 is 1..=5 | 7..=9) = 8;
    same(&mut single, &mut either);
}
