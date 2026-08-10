#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait ArrayMaker<const N: usize> {
    fn make(&self) -> [u8; N + 1];
}

impl<const N: usize> ArrayMaker<N> for () {
    fn make(&self) -> [u8; N + 1] {
        [9_u8; N + 1]
    }
}

fn main() {}
