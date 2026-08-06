// Extracted from library/alloc/src/collections/btree/set/entry.rs:366
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    use std::collections::btree_set::Entry;
    
    let mut set = BTreeSet::new();
    
    if let Entry::Vacant(o) = set.entry("poneyland") {
        o.insert();
    }
    assert!(set.contains("poneyland"));
}
