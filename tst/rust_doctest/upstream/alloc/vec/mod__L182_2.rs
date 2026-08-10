// Extracted from library/alloc/src/vec/mod.rs:182
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec1 = vec![1, 2, 3];
    vec1.push(4);
    let vec2 = Vec::from([1, 2, 3, 4]);
    assert_eq!(vec1, vec2);
}
