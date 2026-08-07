// Extracted from library/alloc/src/sync.rs:3675
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let x = 5;
    let arc = Arc::new(5);

    assert_eq!(Arc::from(x), arc);
}
