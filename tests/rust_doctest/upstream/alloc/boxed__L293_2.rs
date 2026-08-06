// Extracted from library/alloc/src/boxed.rs:293
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {
    
    let zero = Box::<u32>::new_zeroed();
    let zero = unsafe { zero.assume_init() };
    
    assert_eq!(*zero, 0)
}
