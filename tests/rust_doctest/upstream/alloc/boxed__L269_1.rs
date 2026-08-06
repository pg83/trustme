// Extracted from library/alloc/src/boxed.rs:269
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut five = Box::<u32>::new_uninit();
    // Deferred initialization:
    five.write(5);
    let five = unsafe { five.assume_init() };
    
    assert_eq!(*five, 5)
}
