// Extracted from library/alloc/src/collections/btree/set/entry.rs:344
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::btree_set::{Entry, BTreeSet};
    
    let mut set = BTreeSet::new();
    
    match set.entry("poneyland") {
        Entry::Occupied(_) => panic!(),
        Entry::Vacant(v) => assert_eq!(v.into_value(), "poneyland"),
    }
}
