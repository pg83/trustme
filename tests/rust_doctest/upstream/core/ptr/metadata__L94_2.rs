// Extracted from library/core/src/ptr/metadata.rs:94
#![allow(unused)]
#![feature(ptr_metadata)]
fn main() {
    
    assert_eq!(std::ptr::metadata("foo"), 3_usize);
}
