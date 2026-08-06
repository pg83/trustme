// Extracted from library/alloc/src/collections/btree/set.rs:1507
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let set1 = BTreeSet::from([1, 2, 3, 4]);
    let set2: BTreeSet<_> = [1, 2, 3, 4].into();
    assert_eq!(set1, set2);
}
