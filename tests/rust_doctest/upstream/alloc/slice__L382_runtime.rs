// Extracted from library/alloc/src/slice.rs:382
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::alloc::System;

    let s = [10, 40, 30];
    let x = s.to_vec_in(System);
    // Here, `s` and `x` can be modified independently.
}
