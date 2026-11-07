//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Measure {
    const WIDTH: usize;
}
struct Word(u8);
impl Measure for Word {
    const WIDTH: usize = 37;
}

const fn request<T: Measure + ?Sized>(_: &T) -> usize {
    T::WIDTH
}
const fn width() -> usize {
    request(&Word(31))
}

trait Pick {
    type Value;
}
impl Pick for [u8; 37] {
    type Value = u16;
}
type Early = <[u8; width()]>::Value;

fn main() {
    let value: Early = 1237;
    assert_eq!(value, 1237u16);
    assert_eq!(width(), 37);
}
