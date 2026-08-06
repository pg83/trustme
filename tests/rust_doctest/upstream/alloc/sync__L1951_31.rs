// Extracted from library/alloc/src/sync.rs:1951
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let five = Arc::new(5);
    let same_five = Arc::clone(&five);
    let other_five = Arc::new(5);
    
    assert!(Arc::ptr_eq(&five, &same_five));
    assert!(!Arc::ptr_eq(&five, &other_five));
}
