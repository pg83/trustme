// Extracted from library/std/src/collections/hash/set.rs:2145
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    
    let mut set = HashSet::new();
    
    // nonexistent key
    set.entry("poneyland").or_insert();
    assert!(set.contains("poneyland"));
    
    // existing key
    set.entry("poneyland").or_insert();
    assert!(set.contains("poneyland"));
    assert_eq!(set.len(), 1);
}
