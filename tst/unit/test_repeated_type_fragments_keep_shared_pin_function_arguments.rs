#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

macro_rules! aliases {
    ($first:ident, $second:ident, $ty:ty) => {
        type $first = $ty;
        type $second = $ty;
    };
}

aliases!(First, Second, fn(&pin const u16) -> u16);

fn read(value: &pin const u16) -> u16 { *value }

fn main() {
    let value = 29u16;
    let first: First = read;
    let second: Second = read;
    assert_eq!(first(&pin const value), 29);
    assert_eq!(second(&pin const value), 29);
}
