// Extracted from library/alloc/src/rc.rs:1290
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;
    
    let x = Rc::new("hello".to_owned());
    let x_ptr = Rc::into_raw(x);
    
    unsafe {
        // Convert back to an `Rc` to prevent leak.
        let x = Rc::from_raw(x_ptr);
        assert_eq!(&*x, "hello");
    
        // Further calls to `Rc::from_raw(x_ptr)` would be memory-unsafe.
    }
    
    // The memory was freed when `x` went out of scope above, so `x_ptr` is now dangling!
}
