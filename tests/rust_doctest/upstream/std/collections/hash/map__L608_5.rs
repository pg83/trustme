// Extracted from library/std/src/collections/hash/map.rs:608
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut a = HashMap::new();
    assert!(a.is_empty());
    a.insert(1, "a");
    assert!(!a.is_empty());
}
