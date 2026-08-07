// Extracted from library/alloc/src/collections/btree/set/entry.rs:154
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::btree_set::{Entry, BTreeSet};

    let mut set = BTreeSet::<&str>::new();

    let entry_v = match set.entry("a") {
        Entry::Vacant(view) => view,
        Entry::Occupied(_) => unreachable!(),
    };
    entry_v.insert();
    assert!(set.contains("a") && set.len() == 1);

    // Nonexistent key (insert)
    match set.entry("b") {
        Entry::Vacant(view) => view.insert(),
        Entry::Occupied(_) => unreachable!(),
    }
    assert!(set.contains("b") && set.len() == 2);
}
