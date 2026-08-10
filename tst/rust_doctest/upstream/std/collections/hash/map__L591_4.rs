// Extracted from library/std/src/collections/hash/map.rs:591
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut a = HashMap::new();
    assert_eq!(a.len(), 0);
    a.insert(1, "a");
    assert_eq!(a.len(), 1);
}
