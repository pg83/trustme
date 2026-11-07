//@ aux-build: early_seed_headers.rs
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

extern crate early_seed_headers;
use early_seed_headers::{Seed, View, Word};

const fn obtain<T: Seed>() -> T {
    T::VALUE
}
const fn width() -> usize {
    let value: Word = obtain();
    let _object: &dyn View = &value;
    41
}

struct Probe<const N: usize>;
impl Probe<41> {
    type Value = u16;
}
type Early = Probe<{ width() }>::Value;

fn main() {
    let result: Early = 1277;
    assert_eq!(result, 1277u16);
    assert_eq!(width(), 41);
    let value: Word = obtain();
    let object: &dyn View = &value;
    assert_eq!(object.get(), 43);
}
