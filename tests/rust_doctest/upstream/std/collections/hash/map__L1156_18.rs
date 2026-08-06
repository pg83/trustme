// Extracted from library/std/src/collections/hash/map.rs:1156
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map = HashMap::new();
    map.insert(1, "a");
    if let Some(x) = map.get_mut(&1) {
        *x = "b";
    }
    assert_eq!(map[&1], "b");
}
