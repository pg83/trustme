#![feature(generic_const_exprs, pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

const fn next(value: usize) -> usize { value + 1 }

trait Width<const LEN: usize> {}
impl<const LEN: usize> Width<LEN> for [u8; LEN] {}

fn length<T: Width<LEN>, const LEN: usize>(_: T) -> usize { LEN }

struct Carrier<T, const N: usize>(PhantomData<T>);

impl<const N: usize> Carrier<std::pat::pattern_type!(usize is 0..=N), N> {
    fn run() -> usize
    where [(); next(N)]: {
        let values = [0u8; next(N)];
        length(values)
    }
}

fn main() {
    type Bound = std::pat::pattern_type!(usize is 0..=4);
    assert_eq!(Carrier::<Bound, 4>::run(), 5);
}
