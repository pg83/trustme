// Extracted from library/alloc/src/sync.rs:1726
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    let weak_five = Arc::downgrade(&five);
}
