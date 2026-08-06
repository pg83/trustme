// Extracted from library/alloc/src/rc.rs:1196
#![allow(unused)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {
    
    use std::rc::Rc;
    
    let mut five = Rc::<u32>::new_uninit();
    
    // Deferred initialization:
    Rc::get_mut(&mut five).unwrap().write(5);
    
    let five = unsafe { five.assume_init() };
    
    assert_eq!(*five, 5)
}
