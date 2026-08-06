// Extracted from library/std/src/collections/hash/set.rs:175
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let set: HashSet<i32> = HashSet::with_capacity(100);
    assert!(set.capacity() >= 100);
}
