// Extracted from library/std/src/collections/hash/map.rs:729
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut a = HashMap::new();
    a.insert(1, "a");
    a.clear();
    assert!(a.is_empty());
}
