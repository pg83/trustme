//@ compile-flags: -Zdump-mir
#![feature(pattern_types, pattern_type_macro)]

type Digit = std::pat::pattern_type!(u16 is 2..=6);

const fn unpack(tuple: (Digit,)) -> u16 {
    let (value,) = tuple;
    unsafe { std::mem::transmute(value) }
}

const RAW: u16 = unpack((5,));

fn main() {
    assert_eq!(RAW, 5);
}
