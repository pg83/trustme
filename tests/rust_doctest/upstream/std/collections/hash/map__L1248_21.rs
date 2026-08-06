// Extracted from library/std/src/collections/hash/map.rs:1248
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::new();
    map.insert(1, "a");
    assert_eq!(map.remove(&1), Some("a"));
    assert_eq!(map.remove(&1), None);
}
