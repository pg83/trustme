// Extracted from library/std/src/collections/hash/set.rs:237
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut v = HashSet::new();
    assert!(v.is_empty());
    v.insert(1);
    assert!(!v.is_empty());
}
