//@ crate-type: lib
#![feature(const_trait_impl)]

pub const trait Seal {}

pub struct Slot<T>(pub T);

impl<T: const Seal> Slot<T> {}
