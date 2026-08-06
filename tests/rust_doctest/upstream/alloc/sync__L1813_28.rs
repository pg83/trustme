// Extracted from library/alloc/src/sync.rs:1813
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let five = Arc::new(5);
    let _also_five = Arc::clone(&five);
    
    // This assertion is deterministic because we haven't shared
    // the `Arc` between threads.
    assert_eq!(2, Arc::strong_count(&five));
}
