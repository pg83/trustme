// Extracted from library/std/src/collections/hash/set.rs:825
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let a = HashSet::from([1, 2, 3]);
    let mut b = HashSet::new();
    
    assert_eq!(a.is_disjoint(&b), true);
    b.insert(4);
    assert_eq!(a.is_disjoint(&b), true);
    b.insert(1);
    assert_eq!(a.is_disjoint(&b), false);
}
