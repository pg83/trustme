// Extracted from library/alloc/src/sync.rs:519
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {

    use std::sync::Arc;

    let zero = Arc::<u32>::new_zeroed();
    let zero = unsafe { zero.assume_init() };

    assert_eq!(*zero, 0)
}
