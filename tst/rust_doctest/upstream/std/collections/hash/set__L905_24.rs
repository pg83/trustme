// Extracted from library/std/src/collections/hash/set.rs:905
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let mut set = HashSet::new();

    assert_eq!(set.insert(2), true);
    assert_eq!(set.insert(2), false);
    assert_eq!(set.len(), 1);
}
