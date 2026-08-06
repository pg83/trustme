// Extracted from library/alloc/src/vec/mod.rs:1574
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3];
    vec.truncate(8);
    assert_eq!(vec, [1, 2, 3]);
}
