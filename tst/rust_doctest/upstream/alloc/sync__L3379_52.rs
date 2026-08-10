// Extracted from library/alloc/src/sync.rs:3379
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    assert!(five == Arc::new(5));
}
