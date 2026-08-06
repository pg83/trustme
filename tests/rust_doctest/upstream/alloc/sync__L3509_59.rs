// Extracted from library/alloc/src/sync.rs:3509
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    use std::cmp::Ordering;
    
    let five = Arc::new(5);
    
    assert_eq!(Ordering::Less, five.cmp(&Arc::new(6)));
}
