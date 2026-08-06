// Extracted from library/std/src/collections/hash/map.rs:2519
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    map.entry("poneyland").or_insert(12);
    assert_eq!(map.entry("poneyland").key(), &"poneyland");
}
