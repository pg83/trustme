// Extracted from library/std/src/collections/hash/set.rs:2263
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    
    let mut set = HashSet::new();
    assert_eq!(set.entry("poneyland").get(), &"poneyland");
}
