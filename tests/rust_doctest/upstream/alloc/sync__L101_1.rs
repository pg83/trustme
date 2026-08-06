// Extracted from library/alloc/src/sync.rs:101
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let mut data = Arc::new(vec![1, 2, 3]);
    
    // This will clone the vector only if there are other references to it
    Arc::make_mut(&mut data).push(4);
    
    assert_eq!(*data, vec![1, 2, 3, 4]);
}
