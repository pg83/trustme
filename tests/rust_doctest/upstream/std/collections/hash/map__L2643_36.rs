// Extracted from library/std/src/collections/hash/map.rs:2643
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::collections::hash_map::Entry;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    map.entry("poneyland").or_insert(12);
    
    if let Entry::Occupied(mut o) = map.entry("poneyland") {
        assert_eq!(o.insert(15), 12);
    }
    
    assert_eq!(map["poneyland"], 15);
}
