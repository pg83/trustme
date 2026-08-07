// Extracted from library/std/src/collections/hash/set.rs:2303
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::HashSet;
    use std::collections::hash_set::Entry;

    let mut set = HashSet::new();

    if let Entry::Vacant(o) = set.entry("poneyland") {
        o.insert();
    }
    assert!(set.contains("poneyland"));
}
