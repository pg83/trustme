//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(allocator_api)]

use std::alloc::{AllocError, Allocator, Global};

fn initialize<T, A: Allocator>(value: T, allocator: A) -> Result<Box<T, A>, AllocError> {
    let mut boxed = Box::<T, A>::try_new_uninit_in(allocator)?;
    boxed.write(value);
    unsafe { Ok(boxed.assume_init()) }
}

fn main() {
    assert_eq!(*initialize(37usize, Global).unwrap(), 37);
}
