// Extracted from library/std/src/collections/hash/map.rs:631
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut a = HashMap::new();
    a.insert(1, "a");
    a.insert(2, "b");

    for (k, v) in a.drain().take(1) {
        assert!(k == 1 || k == 2);
        assert!(v == "a" || v == "b");
    }

    assert!(a.is_empty());
}
