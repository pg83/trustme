//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -O

#![no_std]
#![crate_name = "own_mutable_reborrow_seed"]

#[inline(never)]
pub fn observe<T>(_: &mut T, count: &mut usize) {
    *count += core::mem::size_of::<T>();
}

pub fn twice<T>(value: &mut T, count: &mut usize) {
    observe(value, count);
    observe(value, count);
}
