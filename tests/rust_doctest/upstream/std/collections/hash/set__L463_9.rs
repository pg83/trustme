// Extracted from library/std/src/collections/hash/set.rs:463
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let mut set: HashSet<i32> = HashSet::new();
    set.reserve(10);
    assert!(set.capacity() >= 10);
}
