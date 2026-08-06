// Extracted from library/alloc/src/vec/mod.rs:2184
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 3, 4];
    vec.retain(|&x| x % 2 == 0);
    assert_eq!(vec, [2, 4]);
}
