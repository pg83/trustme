// Extracted from library/alloc/src/sync.rs:2963
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};
    use std::alloc::System;

    let strong = Arc::new_in("hello".to_owned(), System);
    let weak = Arc::downgrade(&strong);
    let (raw, alloc) = weak.into_raw_with_allocator();

    assert_eq!(1, Arc::weak_count(&strong));
    assert_eq!("hello", unsafe { &*raw });

    drop(unsafe { Weak::from_raw_in(raw, alloc) });
    assert_eq!(0, Arc::weak_count(&strong));
}
