// Extracted from library/alloc/src/sync.rs:2539
#![allow(unused)]
#![feature(arc_is_unique)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    
    let x = Arc::new(3);
    assert!(Arc::is_unique(&x));
    
    let y = Arc::clone(&x);
    assert!(!Arc::is_unique(&x));
    drop(y);
    
    // Weak references also count, because they could be upgraded at any time.
    let z = Arc::downgrade(&x);
    assert!(!Arc::is_unique(&x));
}
