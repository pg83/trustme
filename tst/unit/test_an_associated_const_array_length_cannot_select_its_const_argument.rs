//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width<const N: usize> { const SIZE: usize; }
impl<const N: usize> Width<N> for u8 { const SIZE: usize = N; }

fn bytes<const N: usize>(_: [u8; <u8 as Width<N>>::SIZE])
where [(); <u8 as Width<N>>::SIZE]: {}

fn main() {
    bytes([7u8]);
}
