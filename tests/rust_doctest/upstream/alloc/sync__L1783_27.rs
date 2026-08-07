// Extracted from library/alloc/src/sync.rs:1783
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);
    let _weak_five = Arc::downgrade(&five);

    // This assertion is deterministic because we haven't shared
    // the `Arc` or `Weak` between threads.
    assert_eq!(1, Arc::weak_count(&five));
}
