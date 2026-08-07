// Extracted from library/alloc/src/collections/btree/set/entry.rs:247
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;

    let mut set = BTreeSet::new();
    set.entry("poneyland").or_insert();

    // existing key
    assert_eq!(set.entry("poneyland").get(), &"poneyland");
    // nonexistent key
    assert_eq!(set.entry("horseland").get(), &"horseland");
}
