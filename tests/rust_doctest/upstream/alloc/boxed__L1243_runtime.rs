// Extracted from library/alloc/src/boxed.rs:1243
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::alloc::System;

    let x = Box::new_in(5, System);
    let (ptr, alloc) = Box::into_raw_with_allocator(x);
    let x = unsafe { Box::from_raw_in(ptr, alloc) };
}
