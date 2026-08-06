// Extracted from library/std/src/collections/hash/map.rs:1760
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let map = HashMap::from([
        ("a", 1),
    ]);
    let iter_keys = map.into_values();
}
