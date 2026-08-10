// Extracted from library/alloc/src/collections/btree/set/entry.rs:52
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::btree_set::{Entry, BTreeSet};

    let mut set = BTreeSet::from(["a", "b"]);

    match set.entry("a") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(_) => { }
    }
}
