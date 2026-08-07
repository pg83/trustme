// Extracted from library/alloc/src/sync.rs:1894
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::sync::Arc;
    use std::alloc::System;

    let five = Arc::new_in(5, System);

    unsafe {
        let (ptr, _alloc) = Arc::into_raw_with_allocator(five);
        Arc::increment_strong_count_in(ptr, System);

        // Those assertions are deterministic because we haven't shared
        // the `Arc` between threads.
        let five = Arc::from_raw_in(ptr, System);
        assert_eq!(2, Arc::strong_count(&five));
        Arc::decrement_strong_count_in(ptr, System);
        assert_eq!(1, Arc::strong_count(&five));
    }
}
