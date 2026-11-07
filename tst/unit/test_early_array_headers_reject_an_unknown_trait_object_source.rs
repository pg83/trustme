//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Seed: Sized {
    const VALUE: Self;
}
struct Word(u8);
impl Seed for Word {
    const VALUE: Self = Word(43);
}
impl Seed for u64 {
    const VALUE: Self = 47;
}
trait View {
    fn get(&self) -> u8;
}
impl View for Word {
    fn get(&self) -> u8 { self.0 }
}

const fn obtain<T: Seed>() -> T {
    T::VALUE
}
const fn width() -> usize {
    let value = obtain();
    let _object: &dyn View = &value;
    41
}

trait Pick {
    type Value;
}
impl Pick for [u8; 41] {
    type Value = u16;
}
type Early = <[u8; width()]>::Value;

fn main() {}
