// Extracted from library/std/src/collections/hash/map.rs:1129
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::new();
    map.insert(1, "a");
    assert_eq!(map.contains_key(&1), true);
    assert_eq!(map.contains_key(&2), false);
}
