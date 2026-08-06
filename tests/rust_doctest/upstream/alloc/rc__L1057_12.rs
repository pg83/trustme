// Extracted from library/alloc/src/rc.rs:1057
#![allow(unused)]
#![feature(new_zeroed_alloc)]
extern crate alloc;
fn main() {
    
    use std::rc::Rc;
    
    let values = Rc::<[u32]>::new_zeroed_slice(3);
    let values = unsafe { values.assume_init() };
    
    assert_eq!(*values, [0, 0, 0])
}
