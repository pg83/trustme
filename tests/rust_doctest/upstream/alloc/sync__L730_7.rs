// Extracted from library/alloc/src/sync.rs:730
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::sync::Arc;
    use std::alloc::System;

    let zero = Arc::<u32, _>::new_zeroed_in(System);
    let zero = unsafe { zero.assume_init() };

    assert_eq!(*zero, 0)
}
