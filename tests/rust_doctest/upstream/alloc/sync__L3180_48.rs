// Extracted from library/alloc/src/sync.rs:3180
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let first_rc = Arc::new(5);
    let first = Arc::downgrade(&first_rc);
    let second = Arc::downgrade(&first_rc);

    assert!(first.ptr_eq(&second));

    let third_rc = Arc::new(5);
    let third = Arc::downgrade(&third_rc);

    assert!(!first.ptr_eq(&third));
}
