// Extracted from library/alloc/src/collections/btree/set/entry.rs:71
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::btree_set::{Entry, BTreeSet};
    
    let mut set = BTreeSet::new();
    
    match set.entry("a") {
        Entry::Occupied(_) => unreachable!(),
        Entry::Vacant(_) => { }
    }
}
