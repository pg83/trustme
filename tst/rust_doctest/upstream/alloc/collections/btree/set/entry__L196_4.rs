// Extracted from library/alloc/src/collections/btree/set/entry.rs:196
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();
    let entry = set.entry("horseyland").insert();

    assert_eq!(entry.get(), &"horseyland");
}
