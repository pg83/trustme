// Extracted from library/std/src/collections/hash/set.rs:529
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let mut set = HashSet::with_capacity(100);
    set.insert(1);
    set.insert(2);
    assert!(set.capacity() >= 100);
    set.shrink_to(10);
    assert!(set.capacity() >= 10);
    set.shrink_to(0);
    assert!(set.capacity() >= 2);
}
