#![feature(pattern_types, pattern_type_macro, specialization)]
#![allow(incomplete_features)]

use std::pat::pattern_type;

type Present<T> = pattern_type!(*const T is !null);
struct Holder<T>(T);

trait Rank {
    type Output;
    const TAG: u8;
}

impl Rank for Holder<Present<[u8; 2]>> {
    const TAG: u8 = 3;
}

impl<T> Rank for Holder<Present<T>> {
    default type Output = u16;
    default const TAG: u8 = 1;
}

impl<T> Rank for Holder<Present<[T; 2]>> {
    default type Output = u32;
    default const TAG: u8 = 2;
}

fn main() {
    let value: <Holder<Present<[u8; 2]>> as Rank>::Output = 17u32;
    assert_eq!(value, 17);
    assert_eq!(<Holder<Present<[u8; 2]>> as Rank>::TAG, 3);
    assert_eq!(<Holder<Present<[u16; 2]>> as Rank>::TAG, 2);
    assert_eq!(<Holder<Present<u16>> as Rank>::TAG, 1);
}
