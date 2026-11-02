#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

macro_rules! aliases {
    ($first:ident, $second:ident, $ty:ty) => {
        type $first = $ty;
        type $second = $ty;
    };
}

aliases!(First, Second, for<'a> fn(&'a pin const u16) -> &'a pin const u16);

fn identity<'a>(value: &'a pin const u16) -> &'a pin const u16 { value }

fn main() {
    let value = 43u16;
    let first: First = identity;
    let second: Second = identity;
    assert_eq!(*first(&pin const value), 43);
    assert_eq!(*second(&pin const value), 43);
}
