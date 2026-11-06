//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width { const SIZE: usize = 3; }

fn wrong<T: Width>(array: [u8; T::SIZE]) -> [u8; 3]
where [(); T::SIZE]: { array }

fn main() {}
