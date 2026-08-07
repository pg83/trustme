// Extracted from library/std/src/collections/hash/set.rs:1998
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::hash_set::{Entry, HashSet};

    let mut set = HashSet::new();

    match set.entry("a") {
        Entry::Occupied(_) => unreachable!(),
        Entry::Vacant(_) => { }
    }
}
