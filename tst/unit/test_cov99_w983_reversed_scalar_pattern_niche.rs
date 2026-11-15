//@ compile-fail: cannot transmute between types of different sizes
#![feature(pattern_types, pattern_type_macro)]

type Reversed = std::pat::pattern_type!(u8 is 9..=3);

fn shrink(value: Option<Reversed>) -> u8 {
    unsafe { std::mem::transmute(value) }
}

fn main() {
    std::process::exit(shrink(None) as i32);
}
