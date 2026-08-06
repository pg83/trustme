// Extracted from library/alloc/src/collections/btree/set/entry.rs:275
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::btree_set::{Entry, BTreeSet};
    
    let mut set = BTreeSet::new();
    set.entry("poneyland").or_insert();
    
    match set.entry("poneyland") {
        Entry::Vacant(_) => panic!(),
        Entry::Occupied(entry) => assert_eq!(entry.get(), &"poneyland"),
    }
}
