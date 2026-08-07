// Extracted from library/std/src/hash/random.rs:27
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::hash::RandomState;

    let s = RandomState::new();
    let mut map = HashMap::with_hasher(s);
    map.insert(1, 2);
}
