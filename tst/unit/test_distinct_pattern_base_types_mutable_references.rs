//@ compile-fail: Type mismatch between u32 and i32
#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
type Unsigned = pattern_type!(u32 is 1..=5);
type Signed = pattern_type!(i32 is 1..=5);
fn same<T>(_: &mut T, _: &mut T) {}
fn main() {
    let mut unsigned: Unsigned = 2;
    let mut signed: Signed = 3;
    same(&mut unsigned, &mut signed);
}
