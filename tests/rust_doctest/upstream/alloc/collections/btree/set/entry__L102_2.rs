// Extracted from library/alloc/src/collections/btree/set/entry.rs:102
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::btree_set::{Entry, BTreeSet};
    
    let mut set = BTreeSet::new();
    set.extend(["a", "b", "c"]);
    
    let _entry_o = set.entry("a").insert();
    assert_eq!(set.len(), 3);
    
    // Existing key
    match set.entry("a") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(view) => {
            assert_eq!(view.get(), &"a");
        }
    }
    
    assert_eq!(set.len(), 3);
    
    // Existing key (take)
    match set.entry("c") {
        Entry::Vacant(_) => unreachable!(),
        Entry::Occupied(view) => {
            assert_eq!(view.remove(), "c");
        }
    }
    assert_eq!(set.get(&"c"), None);
    assert_eq!(set.len(), 2);
}
