#![feature(pattern_types, pattern_type_macro)]

use std::pat::pattern_type;

type Digit = pattern_type!(u32 is 1..=9);

fn duplicate<T: Copy>(value: T) -> impl Fn() -> (T, T) {
    move || (value, value)
}

fn main() {
    let value: Digit = 7;
    let pair = duplicate(value)();
    let first: u32 = unsafe { std::mem::transmute(pair.0) };
    let second: u32 = unsafe { std::mem::transmute(pair.1) };
    assert_eq!((first, second), (7, 7));
}
