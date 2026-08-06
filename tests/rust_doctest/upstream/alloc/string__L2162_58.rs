// Extracted from library/alloc/src/string.rs:2162
#![allow(unused)]
extern crate alloc;
fn main() {
    // some invalid bytes, in a vector
    let bytes = vec![0, 159];
    
    let value = String::from_utf8(bytes);
    
    assert_eq!(&[0, 159], value.unwrap_err().as_bytes());
}
