// Extracted from library/alloc/src/collections/btree/set/entry.rs:326
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();
    assert_eq!(set.entry("poneyland").get(), &"poneyland");
}
