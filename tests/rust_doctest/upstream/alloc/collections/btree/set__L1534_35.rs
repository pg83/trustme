// Extracted from library/alloc/src/collections/btree/set.rs:1534
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let set = BTreeSet::from([1, 2, 3, 4]);
    
    let v: Vec<_> = set.into_iter().collect();
    assert_eq!(v, [1, 2, 3, 4]);
}
