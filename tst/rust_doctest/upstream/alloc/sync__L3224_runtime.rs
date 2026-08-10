// Extracted from library/alloc/src/sync.rs:3224
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};

    let weak_five = Arc::downgrade(&Arc::new(5));

    let _ = Weak::clone(&weak_five);
}
