// Extracted from library/alloc/src/sync.rs:1007
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let x = Arc::new(3);
    assert_eq!(Arc::try_unwrap(x), Ok(3));
    
    let x = Arc::new(4);
    let _y = Arc::clone(&x);
    assert_eq!(*Arc::try_unwrap(x).unwrap_err(), 4);
}
