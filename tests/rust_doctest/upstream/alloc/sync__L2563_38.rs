// Extracted from library/alloc/src/sync.rs:2563
#![allow(unused)]
#![feature(arc_is_unique)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    
    let arc = Arc::new(5);
    let pointer: *const i32 = &*arc;
    assert!(Arc::is_unique(&arc));
    assert_eq!(unsafe { *pointer }, 5);
}
