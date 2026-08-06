// Extracted from library/alloc/src/sync.rs:3552
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let x: Arc<i32> = Default::default();
    assert_eq!(*x, 0);
}
