// Extracted from library/std/src/collections/hash/set.rs:1980
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::hash_set::{Entry, HashSet};

    let mut set = HashSet::from(["a", "b"]);

    match set.entry("a") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(_) => { }
    }
}
