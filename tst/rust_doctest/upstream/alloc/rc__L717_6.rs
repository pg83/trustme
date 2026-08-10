// Extracted from library/alloc/src/rc.rs:717
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::rc::Rc;
    use std::alloc::System;

    let zero = Rc::<u32, _>::new_zeroed_in(System);
    let zero = unsafe { zero.assume_init() };

    assert_eq!(*zero, 0)
}
