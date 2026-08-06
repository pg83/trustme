// Extracted from library/alloc/src/collections/btree/set.rs:861
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::new();
    
    set.insert(1);
    while let Some(n) = set.pop_last() {
        assert_eq!(n, 1);
    }
    assert!(set.is_empty());
}
