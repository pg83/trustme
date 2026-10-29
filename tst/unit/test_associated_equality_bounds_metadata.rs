//@ aux-build: associated_equality_bounds.rs
extern crate associated_equality_bounds;

use associated_equality_bounds::{adapt, Counter, Source};

trait Summarize {
    fn sum(&mut self) -> u32;
}

impl<S: Source<Item = u16>> Summarize for S {
    fn sum(&mut self) -> u32 {
        let first = self.next();
        adapt(self, |second| u32::from(first) + u32::from(second))
    }
}

fn total<I: Iterator<Item = u16>>(values: I) -> u16 {
    values.fold(0, |sum, value| sum + value)
}

fn main() {
    let mut counter = Counter(9);
    assert_eq!(counter.sum(), 21);
    assert_eq!(adapt(&mut counter, |value| u32::from(value) * 3), 36);
    assert_eq!(total([2, 5, 9].into_iter()), 16);
}
