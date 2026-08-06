// Extracted from library/alloc/src/collections/btree/set.rs:785
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::new();
    assert_eq!(set.first(), None);
    set.insert(1);
    assert_eq!(set.first(), Some(&1));
    set.insert(2);
    assert_eq!(set.first(), Some(&1));
}
