// Extracted from library/std/src/collections/hash/map.rs:2692
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut map: HashMap<&str, u32> = HashMap::new();
    assert_eq!(map.entry("poneyland").key(), &"poneyland");
}
