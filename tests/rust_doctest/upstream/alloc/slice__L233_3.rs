// Extracted from library/alloc/src/slice.rs:233
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = [4i32, -5, 1, -3, 2];
    
    v.sort_by_key(|k| k.abs());
    assert_eq!(v, [1, 2, -3, 4, -5]);
}
