//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]

trait Source: Sized {
    fn bounded(owner: Self, value: std::pat::pattern_type!(u16 is 3..=9)) -> (Self, u16) {
        (owner, unsafe { std::mem::transmute(value) })
    }
    fn diverging(owner: Self, call: fn(Self) -> !) -> ! { call(owner) }
}
impl Source for u8 {}
reuse Source::{bounded, diverging};

fn main() {
    let low: std::pat::pattern_type!(u16 is 3..=9) = 3;
    let high: std::pat::pattern_type!(u16 is 3..=9) = 9;
    assert_eq!(bounded(11u8, low), (11, 3));
    assert_eq!(bounded(17u8, high), (17, 9));
    let call: fn(u8, fn(u8) -> !) -> ! = diverging::<u8>;
    std::hint::black_box(call);
}
