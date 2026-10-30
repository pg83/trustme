#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(internal_features, incomplete_features)]
use std::pat::pattern_type;
type Below<const END: i16> = pattern_type!(i16 is ..=END);
type Above<const START: i16> = pattern_type!(i16 is START..);
fn endpoints<const END: i16, const START: i16>(_: &Below<END>, _: &Above<START>) -> (i16, i16) {
    (END, START)
}
fn main() {
    let lower: Below<4> = 3;
    let upper: Above<9> = 13;
    assert_eq!(endpoints(&lower, &upper), (4, 9));
    let lower: i16 = unsafe { std::mem::transmute(lower) };
    let upper: i16 = unsafe { std::mem::transmute(upper) };
    assert_eq!((lower, upper), (3, 13));
}
