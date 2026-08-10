// Extracted from library/std/src/collections/hash/set.rs:2281
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::hash_set::{Entry, HashSet};

    let mut set = HashSet::new();

    match set.entry("poneyland") {
        Entry::Occupied(_) => panic!(),
        Entry::Vacant(v) => assert_eq!(v.into_value(), "poneyland"),
    }
}
