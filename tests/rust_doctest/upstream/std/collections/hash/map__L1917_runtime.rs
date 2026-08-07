// Extracted from library/std/src/collections/hash/map.rs:1917
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);

    // Not possible with .iter()
    let vec: Vec<(&str, i32)> = map.into_iter().collect();
}
