// Extracted from library/std/src/collections/hash/set.rs:352
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut v = HashSet::new();
    v.insert(1);
    v.clear();
    assert!(v.is_empty());
}
