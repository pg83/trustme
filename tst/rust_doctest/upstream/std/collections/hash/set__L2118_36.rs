// Extracted from library/std/src/collections/hash/set.rs:2118
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::HashSet;

    let mut set = HashSet::new();
    let entry = set.entry("horseyland").insert();

    assert_eq!(entry.get(), &"horseyland");
}
