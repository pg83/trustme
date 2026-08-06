// Extracted from library/std/src/collections/hash/set.rs:2177
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    
    let mut set = HashSet::new();
    set.entry("poneyland").or_insert();
    
    // existing key
    assert_eq!(set.entry("poneyland").get(), &"poneyland");
    // nonexistent key
    assert_eq!(set.entry("horseland").get(), &"horseland");
}
