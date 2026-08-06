// Extracted from library/std/src/collections/hash/map.rs:2708
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::collections::hash_map::Entry;
    
    let mut map: HashMap<&str, u32> = HashMap::new();
    
    if let Entry::Vacant(v) = map.entry("poneyland") {
        v.into_key();
    }
}
