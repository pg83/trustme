// Extracted from library/std/src/collections/hash/map.rs:2586
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::collections::hash_map::Entry;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    map.entry("poneyland").or_insert(12);
    
    assert_eq!(map["poneyland"], 12);
    if let Entry::Occupied(mut o) = map.entry("poneyland") {
        *o.get_mut() += 10;
        assert_eq!(*o.get(), 22);
    
        // We can use the same Entry multiple times.
        *o.get_mut() += 2;
    }
    
    assert_eq!(map["poneyland"], 24);
}
