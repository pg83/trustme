// Extracted from library/std/src/collections/hash/map.rs:896
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map = HashMap::new();
    map.insert(1, "a");
    assert_eq!(map.get(&1), Some(&"a"));
    assert_eq!(map.get(&2), None);
}
