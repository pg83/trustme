// Extracted from library/std/src/collections/hash/set.rs:2205
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::hash_set::{Entry, HashSet};

    let mut set = HashSet::new();
    set.entry("poneyland").or_insert();

    match set.entry("poneyland") {
        Entry::Vacant(_) => panic!(),
        Entry::Occupied(entry) => assert_eq!(entry.get(), &"poneyland"),
    }
}
