// Extracted from library/alloc/src/collections/btree/set.rs:1076
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::from([1, 2, 3]);
    assert_eq!(set.take(&2), Some(2));
    assert_eq!(set.take(&2), None);
}
