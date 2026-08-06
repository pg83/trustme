// Extracted from library/alloc/src/vec/mod.rs:3830
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![1, 5];
    let new = [2, 3, 4];
    v.splice(1..1, new);
    assert_eq!(v, [1, 2, 3, 4, 5]);
}
