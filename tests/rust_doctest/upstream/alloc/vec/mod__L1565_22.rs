// Extracted from library/alloc/src/vec/mod.rs:1565
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3, 4, 5];
    vec.truncate(2);
    assert_eq!(vec, [1, 2]);
}
