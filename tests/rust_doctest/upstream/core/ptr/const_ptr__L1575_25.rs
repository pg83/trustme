// Extracted from library/core/src/ptr/const_ptr.rs:1575
#![allow(unused)]
#![feature(array_ptr_get)]
fn main() {
    
    let arr: *const [i32; 3] = &[1, 2, 4] as *const [i32; 3];
    let slice: *const [i32] = arr.as_slice();
    assert_eq!(slice.len(), 3);
}
