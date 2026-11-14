//@ compile-fail: cov99_into_bool
#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

trait BoolBits {
    fn cov99_into_bool(self);
}

impl<T> BoolBits for T
where
    bool: TransmuteFrom<T, { Assume { alignment: false, lifetimes: false, safety: true, validity: false } }>,
{
    fn cov99_into_bool(self) {}
}

fn main() {
    2u8.cov99_into_bool();
}
