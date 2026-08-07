// Extracted from library/alloc/src/sync.rs:2296
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let mut data = Arc::new(75);
    let weak = Arc::downgrade(&data);

    assert!(75 == *data);
    assert!(75 == *weak.upgrade().unwrap());

    *Arc::make_mut(&mut data) += 1;

    assert!(76 == *data);
    assert!(weak.upgrade().is_none());
}
