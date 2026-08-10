// Extracted from library/alloc/src/sync.rs:2840
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};

    let strong = Arc::new("hello".to_owned());

    let raw_1 = Arc::downgrade(&strong).into_raw();
    let raw_2 = Arc::downgrade(&strong).into_raw();

    assert_eq!(2, Arc::weak_count(&strong));

    assert_eq!("hello", &*unsafe { Weak::from_raw(raw_1) }.upgrade().unwrap());
    assert_eq!(1, Arc::weak_count(&strong));

    drop(strong);

    // Decrement the last weak count.
    assert!(unsafe { Weak::from_raw(raw_2) }.upgrade().is_none());
}
