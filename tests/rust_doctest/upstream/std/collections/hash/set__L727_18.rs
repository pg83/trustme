// Extracted from library/std/src/collections/hash/set.rs:727
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::HashSet;

    let mut set = HashSet::from([1, 2, 3]);
    assert_eq!(set.len(), 3);
    assert_eq!(set.get_or_insert(2), &2);
    assert_eq!(set.get_or_insert(100), &100);
    assert_eq!(set.len(), 4); // 100 was inserted
}
