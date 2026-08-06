// Extracted from library/alloc/src/slice.rs:117
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = [4, -5, 1, -3, 2];
    
    v.sort();
    assert_eq!(v, [-5, -3, 1, 2, 4]);
}
