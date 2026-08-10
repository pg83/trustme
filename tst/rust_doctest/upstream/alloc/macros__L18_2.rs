// Extracted from library/alloc/src/macros.rs:18
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec![1; 3];
    assert_eq!(v, [1, 1, 1]);
}
