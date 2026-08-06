// Extracted from library/std/src/collections/hash/set.rs:952
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut set = HashSet::new();
    
    set.insert(2);
    assert_eq!(set.remove(&2), true);
    assert_eq!(set.remove(&2), false);
}
