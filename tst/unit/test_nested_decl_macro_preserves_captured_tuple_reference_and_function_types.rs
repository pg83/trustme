#![feature(decl_macro)]

macro_rules! define_type {
    ($name:ident, $ty:ty) => {
        macro $name() { $ty }
    };
}

define_type!(selected_type, (&'static [u16; 2], fn(u16) -> u16));

static WORDS: [u16; 2] = [41, 43];

fn increment(value: u16) -> u16 { value + 2 }

fn main() {
    let value: selected_type!() = (&WORDS, increment);
    assert_eq!(value.0[1], 43);
    assert_eq!((value.1)(value.0[0]), 43);
}
