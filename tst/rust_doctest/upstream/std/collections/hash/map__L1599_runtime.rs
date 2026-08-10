// Extracted from library/std/src/collections/hash/map.rs:1599
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map = HashMap::from([
        ("a", 1),
    ]);
    let iter_values = map.values();
}
