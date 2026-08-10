// Extracted from library/alloc/src/sync.rs:399
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);
}
