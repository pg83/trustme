// Extracted from library/std/src/collections/hash/map.rs:496
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);

    let mut vec: Vec<i32> = map.into_values().collect();
    // The `IntoValues` iterator produces values in arbitrary order, so
    // the values must be sorted to test them against a sorted array.
    vec.sort_unstable();
    assert_eq!(vec, [1, 2, 3]);
}
