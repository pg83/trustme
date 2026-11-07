//@ aux-build: early_header_values.rs
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

extern crate early_header_values;
use early_header_values::{MakeSeed, Mark};

const fn obtain<T: MakeSeed>() -> T {
    T::ITEM
}
const fn width() -> usize {
    let mut value = obtain();
    value = Mark(41);
    value.0
}

struct Packet<const N: usize>;
impl Packet<41> {
    type Output = u16;
}
impl Packet<43> {
    type Output = u32;
}
type Early = Packet<{ width() }>::Output;

fn main() {
    let value: Early = 1319u16;
    assert_eq!(value, 1319);
    assert_eq!(width(), 41);
    assert_eq!(std::mem::size_of::<Early>(), 2);
}
