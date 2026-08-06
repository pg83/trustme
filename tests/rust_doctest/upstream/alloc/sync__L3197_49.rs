// Extracted from library/alloc/src/sync.rs:3197
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};
    
    let first = Weak::new();
    let second = Weak::new();
    assert!(first.ptr_eq(&second));
    
    let third_rc = Arc::new(());
    let third = Arc::downgrade(&third_rc);
    assert!(!first.ptr_eq(&third));
}
