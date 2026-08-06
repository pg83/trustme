// Extracted from library/alloc/src/sync.rs:2879
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::{Arc, Weak};
    
    let strong = Arc::new("hello".to_owned());
    let weak = Arc::downgrade(&strong);
    let raw = weak.into_raw();
    
    assert_eq!(1, Arc::weak_count(&strong));
    assert_eq!("hello", unsafe { &*raw });
    
    drop(unsafe { Weak::from_raw(raw) });
    assert_eq!(0, Arc::weak_count(&strong));
}
