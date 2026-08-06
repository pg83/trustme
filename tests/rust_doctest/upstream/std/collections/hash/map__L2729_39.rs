// Extracted from library/std/src/collections/hash/map.rs:2729
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::collections::hash_map::Entry;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    
    if let Entry::Vacant(o) = map.entry("poneyland") {
        o.insert(37);
    }
    assert_eq!(map["poneyland"], 37);
}
