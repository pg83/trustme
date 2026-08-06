// Extracted from library/alloc/src/collections/btree/set.rs:942
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::from([1, 2, 3]);
    assert_eq!(set.len(), 3);
    assert_eq!(set.get_or_insert(2), &2);
    assert_eq!(set.get_or_insert(100), &100);
    assert_eq!(set.len(), 4); // 100 was inserted
}
