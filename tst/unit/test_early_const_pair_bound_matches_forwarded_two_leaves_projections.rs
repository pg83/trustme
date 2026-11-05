//@ compile-variant: -O
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Palette { type First; type Second; type LeafA; type LeafB; }
trait Repeated {}
impl<T> Repeated for (T, T) {}

struct Counter;
impl Counter {
    const fn index<T: Palette>() -> usize
    where (T::First, T::Second): Repeated {
        5
    }
}

struct Gate<T>(T);
impl Gate<[u8; 5]> { type Value = u16; }
type Selected = Gate<[u8; Counter::index::<u8>()]>::Value;

impl Palette for u8 { type First = Self::LeafA; type Second = Self::LeafB; type LeafA = u16; type LeafB = u16; }

fn main() {
    let value: Selected = 47;
    assert_eq!(value, 47u16);
}
