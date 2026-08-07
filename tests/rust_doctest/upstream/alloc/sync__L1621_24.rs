// Extracted from library/alloc/src/sync.rs:1621
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let x = Arc::new("hello".to_owned());
    let y = Arc::clone(&x);
    let x_ptr = Arc::as_ptr(&x);
    assert_eq!(x_ptr, Arc::as_ptr(&y));
    assert_eq!(unsafe { &*x_ptr }, "hello");
}
