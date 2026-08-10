// Extracted from library/std/src/collections/hash/map.rs:747
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::hash::RandomState;

    let hasher = RandomState::new();
    let map: HashMap<i32, i32> = HashMap::with_hasher(hasher);
    let hasher: &RandomState = map.hasher();
}
