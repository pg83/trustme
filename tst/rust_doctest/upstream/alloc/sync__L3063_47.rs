// Extracted from library/alloc/src/sync.rs:3063
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    let weak_five = Arc::downgrade(&five);

    let strong_five: Option<Arc<_>> = weak_five.upgrade();
    assert!(strong_five.is_some());

    // Destroy all strong pointers.
    drop(strong_five);
    drop(five);

    assert!(weak_five.upgrade().is_none());
}
