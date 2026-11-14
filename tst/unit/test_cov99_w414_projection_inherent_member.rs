//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

pub struct Carrier;

impl Carrier {
    pub type Member = u8;
}

pub trait Source {
    type Out;
}

pub fn identity<T: Source<Out = Carrier>>(value: <T::Out>::Member) -> u8 {
    value
}
