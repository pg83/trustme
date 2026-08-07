// Extracted from library/alloc/src/sync.rs:1510
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    unsafe {
        let ptr = Arc::into_raw(five);
        Arc::increment_strong_count(ptr);

        // This assertion is deterministic because we haven't shared
        // the `Arc` between threads.
        let five = Arc::from_raw(ptr);
        assert_eq!(2, Arc::strong_count(&five));
      // Prevent leaks for Miri.
      Arc::decrement_strong_count(ptr);
    }
}
