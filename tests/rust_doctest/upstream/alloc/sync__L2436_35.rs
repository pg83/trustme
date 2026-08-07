// Extracted from library/alloc/src/sync.rs:2436
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let mut x = Arc::new(3);
    *Arc::get_mut(&mut x).unwrap() = 4;
    assert_eq!(*x, 4);

    let _y = Arc::clone(&x);
    assert!(Arc::get_mut(&mut x).is_none());
}
