// Extracted from library/alloc/src/collections/btree/set.rs:1099
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::from([1, 2, 3, 4, 5, 6]);
    // Keep only the even numbers.
    set.retain(|&k| k % 2 == 0);
    assert!(set.iter().eq([2, 4, 6].iter()));
}
