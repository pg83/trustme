#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;

type Small = pattern_type!(u32 is 1..=5);
type Again = pattern_type!(u32 is 1..=5);
type Larger = pattern_type!(u32 is 1..=9);

trait Uniform { fn width(self) -> usize; }
impl<T> Uniform for (T, T) {
    fn width(self) -> usize { std::mem::size_of::<T>() }
}
trait Pick { fn pick(self) -> u8; }
impl Pick for (Small, Larger) { fn pick(self) -> u8 { 7 } }
impl Pick for (Larger, Small) { fn pick(self) -> u8 { 9 } }

fn main() {
    let a: Small = 2;
    let b: Again = 4;
    assert_eq!((a, b).width(), 4);
    let c: Larger = 8;
    assert_eq!((a, c).pick(), 7);
    assert_eq!((c, b).pick(), 9);
}
