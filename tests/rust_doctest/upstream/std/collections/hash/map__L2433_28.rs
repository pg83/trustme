// Extracted from library/std/src/collections/hash/map.rs:2433
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map: HashMap<&str, u32> = HashMap::new();

    map.entry("poneyland")
       .and_modify(|e| { *e += 1 })
       .or_insert(42);
    assert_eq!(map["poneyland"], 42);

    map.entry("poneyland")
       .and_modify(|e| { *e += 1 })
       .or_insert(42);
    assert_eq!(map["poneyland"], 43);
}
