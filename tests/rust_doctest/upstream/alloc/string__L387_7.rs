// Extracted from library/alloc/src/string.rs:387
#![allow(unused)]
extern crate alloc;
fn main() {
    // some invalid bytes, in a vector
    let bytes = vec![0, 159];
    
    let value = String::from_utf8(bytes);
    
    assert!(value.is_err());
    assert_eq!(vec![0, 159], value.unwrap_err().into_bytes());
}
