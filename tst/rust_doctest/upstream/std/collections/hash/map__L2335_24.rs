// Extracted from library/std/src/collections/hash/map.rs:2335
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map: HashMap<&str, u32> = HashMap::new();

    map.entry("poneyland").or_insert(3);
    assert_eq!(map["poneyland"], 3);

    *map.entry("poneyland").or_insert(10) *= 2;
    assert_eq!(map["poneyland"], 6);
}
