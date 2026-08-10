// Extracted from library/std/src/collections/hash/map.rs:401
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let map = HashMap::from([
        ("a", 1),
        ("b", 2),
        ("c", 3),
    ]);

    let mut vec: Vec<&str> = map.into_keys().collect();
    // The `IntoKeys` iterator produces keys in arbitrary order, so the
    // keys must be sorted to test them against a sorted array.
    vec.sort_unstable();
    assert_eq!(vec, ["a", "b", "c"]);
}
