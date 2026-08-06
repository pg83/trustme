// Extracted from library/alloc/src/vec/mod.rs:3819
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![1, 2, 3, 4];
    let new = [7, 8, 9];
    let u: Vec<_> = v.splice(1..3, new).collect();
    assert_eq!(v, [1, 7, 8, 9, 4]);
    assert_eq!(u, [2, 3]);
}
