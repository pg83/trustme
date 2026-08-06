// Extracted from library/std/src/collections/hash/set.rs:381
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    use std::hash::RandomState;
    
    let s = RandomState::new();
    let mut set = HashSet::with_hasher(s);
    set.insert(2);
}
