// Extracted from library/alloc/src/collections/btree/set.rs:1205
#![allow(unused)]
#![feature(btree_extract_if)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    // Splitting a set into even and odd values, reusing the original set:
    let mut set: BTreeSet<i32> = (0..8).collect();
    let evens: BTreeSet<_> = set.extract_if(.., |v| v % 2 == 0).collect();
    let odds = set;
    assert_eq!(evens.into_iter().collect::<Vec<_>>(), vec![0, 2, 4, 6]);
    assert_eq!(odds.into_iter().collect::<Vec<_>>(), vec![1, 3, 5, 7]);
    
    // Splitting a set into low and high halves, reusing the original set:
    let mut set: BTreeSet<i32> = (0..8).collect();
    let low: BTreeSet<_> = set.extract_if(0..4, |_v| true).collect();
    let high = set;
    assert_eq!(low.into_iter().collect::<Vec<_>>(), [0, 1, 2, 3]);
    assert_eq!(high.into_iter().collect::<Vec<_>>(), [4, 5, 6, 7]);
}
