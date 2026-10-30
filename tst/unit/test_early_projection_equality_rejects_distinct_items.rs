//@ compile-fail: Type mismatch
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Family {
    type Input;
    type Output;
    type Other;
}

impl Family for u8 {
    type Input = Self::Other;
    type Output = u16;
    type Other = u16;
}

impl Family for u16 {
    type Input = u32;
    type Output = u32;
    type Other = u32;
}

struct Runner;

impl Runner {
    const fn length<T: Family>() -> usize
    where
        u8: Family<Input = T::Output>,
    {
        3
    }
}

trait Length {}
impl Length for [u8; 3] {}

struct Holder<T>(T);
impl<T: Length> Holder<T> {
    type Array = [u8; 17];
}

type Selected = Holder<[u8; Runner::length::<u16>()]>::Array;

fn main() {}
