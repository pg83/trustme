//@ compile-fail: where clauses are not allowed before const item bodies
#![feature(generic_const_items)]

const WIDTH<T>: usize where T: Copy = core::mem::size_of::<T>();

fn main() {}
