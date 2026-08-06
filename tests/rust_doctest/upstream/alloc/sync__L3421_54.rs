// Extracted from library/alloc/src/sync.rs:3421
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    use std::cmp::Ordering;
    
    let five = Arc::new(5);
    
    assert_eq!(Some(Ordering::Less), five.partial_cmp(&Arc::new(6)));
}
