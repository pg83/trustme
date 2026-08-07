// Extracted from library/alloc/src/sync.rs:1550
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    unsafe {
        let ptr = Arc::into_raw(five);
        Arc::increment_strong_count(ptr);

        // Those assertions are deterministic because we haven't shared
        // the `Arc` between threads.
        let five = Arc::from_raw(ptr);
        assert_eq!(2, Arc::strong_count(&five));
        Arc::decrement_strong_count(ptr);
        assert_eq!(1, Arc::strong_count(&five));
    }
}
