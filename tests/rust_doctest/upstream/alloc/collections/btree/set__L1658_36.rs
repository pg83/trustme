// Extracted from library/alloc/src/collections/btree/set.rs:1658
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let a = BTreeSet::from([1, 2, 3]);
    let b = BTreeSet::from([3, 4, 5]);
    
    let result = &a - &b;
    assert_eq!(result, BTreeSet::from([1, 2]));
}
