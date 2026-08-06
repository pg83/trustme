// Extracted from library/alloc/src/collections/btree/set.rs:630
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let set = BTreeSet::from([1, 2, 3]);
    assert_eq!(set.get(&2), Some(&2));
    assert_eq!(set.get(&4), None);
}
