// Extracted from library/std/src/collections/hash/map.rs:1189
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::new();
    assert_eq!(map.insert(37, "a"), None);
    assert_eq!(map.is_empty(), false);
    
    map.insert(37, "b");
    assert_eq!(map.insert(37, "c"), Some("b"));
    assert_eq!(map[&37], "c");
}
