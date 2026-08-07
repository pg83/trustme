// Extracted from library/std/src/collections/hash/map.rs:1552
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map = HashMap::from([
        ("a", 1),
    ]);
    let iter_keys = map.keys();
}
