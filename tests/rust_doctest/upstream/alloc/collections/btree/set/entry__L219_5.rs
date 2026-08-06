// Extracted from library/alloc/src/collections/btree/set/entry.rs:219
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::new();
    
    // nonexistent key
    set.entry("poneyland").or_insert();
    assert!(set.contains("poneyland"));
    
    // existing key
    set.entry("poneyland").or_insert();
    assert!(set.contains("poneyland"));
    assert_eq!(set.len(), 1);
}
