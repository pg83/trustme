// Extracted from library/std/src/collections/hash/map.rs:2388
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map: HashMap<&str, usize> = HashMap::new();
    
    map.entry("poneyland").or_insert_with_key(|key| key.chars().count());
    
    assert_eq!(map["poneyland"], 9);
}
