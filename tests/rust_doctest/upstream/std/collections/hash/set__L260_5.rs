// Extracted from library/std/src/collections/hash/set.rs:260
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let mut set = HashSet::from([1, 2, 3]);
    assert!(!set.is_empty());

    // print 1, 2, 3 in an arbitrary order
    for i in set.drain() {
        println!("{i}");
    }

    assert!(set.is_empty());
}
