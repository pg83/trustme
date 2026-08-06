// Extracted from library/std/src/collections/hash/set.rs:431
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    use std::hash::RandomState;
    
    let hasher = RandomState::new();
    let set: HashSet<i32> = HashSet::with_hasher(hasher);
    let hasher: &RandomState = set.hasher();
}
