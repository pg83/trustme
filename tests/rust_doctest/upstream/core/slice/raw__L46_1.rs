// Extracted from library/core/src/slice/raw.rs:46
#![allow(unused)]
fn main() {
    use std::slice;
    
    // manifest a slice for a single element
    let x = 42;
    let ptr = &x as *const _;
    let slice = unsafe { slice::from_raw_parts(ptr, 1) };
    assert_eq!(slice[0], 42);
}
