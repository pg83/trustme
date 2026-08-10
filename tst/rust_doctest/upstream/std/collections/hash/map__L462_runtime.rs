// Extracted from library/std/src/collections/hash/map.rs:462
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);

    for val in map.values_mut() {
        *val = *val + 10;
    }

    for val in map.values() {
        println!("{val}");
    }
}
