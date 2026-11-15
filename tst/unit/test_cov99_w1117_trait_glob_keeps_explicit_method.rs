#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Readings {
    fn left(&self) -> u32;
    fn right(&self) -> u32;
}

impl Readings for u32 {
    fn left(&self) -> u32 {
        *self
    }

    fn right(&self) -> u32 {
        *self + 1
    }
}

struct Wrapped(u32);

impl Readings for Wrapped {
    fn left(&self) -> u32 {
        self.0 + 10
    }

    reuse Readings::* { self.0 }
}

fn main() {
    let value = Wrapped(41);
    assert_eq!(value.left(), 51);
    assert_eq!(value.right(), 42);
}
