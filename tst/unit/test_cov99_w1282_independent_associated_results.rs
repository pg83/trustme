//@ crate-type: lib
//@ compile-flags: --emit=metadata

#![no_std]

pub trait Projection {
    type Output;
}

fn require<T: Projection<Output = U>, U>(_: &T) {}

pub fn repeated<T: Projection>(value: &T) {
    require(value);
    require(value);
    require::<T, T::Output>(value);
}
