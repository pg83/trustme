// Extracted from library/alloc/src/vec/mod.rs:2709
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3];
    let mut vec2 = vec![4, 5, 6];
    vec.append(&mut vec2);
    assert_eq!(vec, [1, 2, 3, 4, 5, 6]);
    assert_eq!(vec2, []);
}
