// Extracted from library/alloc/src/boxed.rs:663
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {

    let values = Box::<[u32]>::new_zeroed_slice(3);
    let values = unsafe { values.assume_init() };

    assert_eq!(*values, [0, 0, 0])
}
