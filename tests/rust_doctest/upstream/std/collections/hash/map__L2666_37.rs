// Extracted from library/std/src/collections/hash/map.rs:2666
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::collections::hash_map::Entry;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    map.entry("poneyland").or_insert(12);
    
    if let Entry::Occupied(o) = map.entry("poneyland") {
        assert_eq!(o.remove(), 12);
    }
    
    assert_eq!(map.contains_key("poneyland"), false);
}
