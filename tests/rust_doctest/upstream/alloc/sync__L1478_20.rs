// Extracted from library/alloc/src/sync.rs:1478
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let x = Arc::new("hello".to_owned());
    let x_ptr = Arc::into_raw(x);
    assert_eq!(unsafe { &*x_ptr }, "hello");
    // Prevent leaks for Miri.
    drop(unsafe { Arc::from_raw(x_ptr) });
}
