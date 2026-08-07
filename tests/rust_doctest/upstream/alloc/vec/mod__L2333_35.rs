// Extracted from library/alloc/src/vec/mod.rs:2333
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![10, 20, 21, 30, 20];

    vec.dedup_by_key(|i| *i / 10);

    assert_eq!(vec, [10, 20, 30, 20]);
}
