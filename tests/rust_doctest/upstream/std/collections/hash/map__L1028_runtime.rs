// Extracted from library/std/src/collections/hash/map.rs:1028
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut libraries = HashMap::new();
    libraries.insert("Athenæum".to_string(), 1807);

    // Duplicate keys panic!
    let got = libraries.get_disjoint_mut([
        "Athenæum",
        "Athenæum",
    ]);
}
