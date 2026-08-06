// Extracted from library/std/src/collections/hash/set.rs:2080
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::hash_set::{Entry, HashSet};
    
    let mut set = HashSet::<&str>::new();
    
    let entry_v = match set.entry("a") {
        Entry::Vacant(view) => view,
        Entry::Occupied(_) => unreachable!(),
    };
    entry_v.insert();
    assert!(set.contains("a") && set.len() == 1);
    
    // Nonexistent key (insert)
    match set.entry("b") {
        Entry::Vacant(view) => view.insert(),
        Entry::Occupied(_) => unreachable!(),
    }
    assert!(set.contains("b") && set.len() == 2);
}
