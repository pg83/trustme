// Extracted from library/std/src/collections/hash/set.rs:680
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    
    let set = HashSet::from([1, 2, 3]);
    assert_eq!(set.contains(&1), true);
    assert_eq!(set.contains(&4), false);
}
