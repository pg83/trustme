// Extracted from library/alloc/src/sync.rs:1200
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {

    use std::sync::Arc;

    let values = Arc::<[u32]>::new_zeroed_slice(3);
    let values = unsafe { values.assume_init() };

    assert_eq!(*values, [0, 0, 0])
}
