#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Family {
    type Input;
    type Output;
}

impl Family for u8 {
    type Input = Self::Output;
    type Output = u16;
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

type Selected = Holder<[u8; Runner::length::<u8>()]>::Array;

fn main() {
    let value: Selected = [19; 17];
    assert_eq!(value, [19; 17]);
}
