// Extracted from library/std/src/collections/hash/set.rs:2030
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {

    use std::collections::hash_set::{Entry, HashSet};

    let mut set = HashSet::new();
    set.extend(["a", "b", "c"]);

    let _entry_o = set.entry("a").insert();
    assert_eq!(set.len(), 3);

    // Existing key
    match set.entry("a") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(view) => {
            assert_eq!(view.get(), &"a");
        }
    }

    assert_eq!(set.len(), 3);

    // Existing key (take)
    match set.entry("c") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(view) => {
            assert_eq!(view.remove(), "c");
        }
    }
    assert_eq!(set.get(&"c"), None);
    assert_eq!(set.len(), 2);
}
