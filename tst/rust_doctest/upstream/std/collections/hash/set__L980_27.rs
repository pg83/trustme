// Extracted from library/std/src/collections/hash/set.rs:980
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let mut set = HashSet::from([1, 2, 3]);
    assert_eq!(set.take(&2), Some(2));
    assert_eq!(set.take(&2), None);
}
