// compile-pass
// edition:2024
// mrustc-flags: -Znext-solver

#![feature(ptr_metadata)]

use std::ptr::{self, Pointee};

unsafe fn rebuild<'a, Args: ?Sized>(
    value: *const (),
    metadata: <Args as Pointee>::Metadata,
) -> &'a Args {
    let pointer = ptr::from_raw_parts(value, metadata);
    unsafe { &*pointer }
}

fn main() {}
