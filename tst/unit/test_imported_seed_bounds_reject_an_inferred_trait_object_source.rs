//@ compile-fail: error:
//@ aux-build: early_seed_headers.rs
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

extern crate early_seed_headers;
use early_seed_headers::{Seed, View};

const fn obtain<T: Seed>() -> T {
    T::VALUE
}
const fn width() -> usize {
    let value = obtain();
    let _object: &dyn View = &value;
    41
}

struct Probe<const N: usize>;
impl Probe<41> {
    type Value = u16;
}
type Early = Probe<{ width() }>::Value;

fn main() {}
