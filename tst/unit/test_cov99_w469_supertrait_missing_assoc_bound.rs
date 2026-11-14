//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![no_std]

pub trait Bound {}

pub trait Carrier {
    type Item;
}

pub trait Merged: Carrier<Item: Bound> + Carrier {}

pub fn require_merged_item<T: Merged>() {
    fn require_bound<U: Bound>() {}
    require_bound::<T::Item>();
}
