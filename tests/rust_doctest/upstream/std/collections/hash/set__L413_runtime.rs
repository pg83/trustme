// Extracted from library/std/src/collections/hash/set.rs:413
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    use std::hash::RandomState;

    let s = RandomState::new();
    let mut set = HashSet::with_capacity_and_hasher(10, s);
    set.insert(1);
}
