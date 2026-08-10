// Extracted from library/alloc/src/macros.rs:9
#![allow(unused)]
extern crate alloc;
fn main() {
    let v = vec![1, 2, 3];
    assert_eq!(v[0], 1);
    assert_eq!(v[1], 2);
    assert_eq!(v[2], 3);
}
