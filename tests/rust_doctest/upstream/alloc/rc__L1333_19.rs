// Extracted from library/alloc/src/rc.rs:1333
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let x = Rc::new("hello".to_owned());
    let x_ptr = Rc::into_raw(x);
    assert_eq!(unsafe { &*x_ptr }, "hello");
    // Prevent leaks for Miri.
    drop(unsafe { Rc::from_raw(x_ptr) });
}
