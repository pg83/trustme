//@ compile-fail: error:
#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

struct Carrier<T, const N: usize>(PhantomData<T>);

impl<const N: usize> Carrier<std::pat::pattern_type!(usize is 0..=N), N> {
    fn run() -> u8 { 43 }
}

fn main() {
    type Outside = std::pat::pattern_type!(usize is 10..=12);
    let _ = Carrier::<Outside, 4>::run();
}
