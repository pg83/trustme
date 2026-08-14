#![feature(fn_delegation)]
#![allow(incomplete_features)]

use core::fmt;

struct Wrapped(&'static str);

impl fmt::Display for Wrapped {
    reuse <str as fmt::Display>::fmt { self.0 }
}

trait Pair<T> {
    fn pair<U>(&self, value: U, other: T) -> (T, U) {
        (other, value)
    }
}

impl<T> Pair<T> for u8 {}

reuse Pair::pair as pair;

fn main() {
    assert_eq!(format!("{}", Wrapped("external")), "external");
    assert_eq!(pair(&0u8, "method", 7u16), (7u16, "method"));
}
