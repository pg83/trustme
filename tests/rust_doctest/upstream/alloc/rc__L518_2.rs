// Extracted from library/alloc/src/rc.rs:518
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {

    use std::rc::Rc;

    let zero = Rc::<u32>::new_zeroed();
    let zero = unsafe { zero.assume_init() };

    assert_eq!(*zero, 0)
}
