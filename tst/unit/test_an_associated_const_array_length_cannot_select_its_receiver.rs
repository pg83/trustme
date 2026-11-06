//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width { const SIZE: usize; }
impl Width for u8 { const SIZE: usize = 1; }
impl Width for u16 { const SIZE: usize = 2; }

fn bytes<T: Width>(_: [u8; T::SIZE]) where [(); T::SIZE]: {}

fn main() {
    bytes([7u8]);
}
