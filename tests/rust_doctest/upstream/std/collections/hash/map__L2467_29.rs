// Extracted from library/std/src/collections/hash/map.rs:2467
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    
    let mut map: HashMap<&str, String> = HashMap::new();
    let entry = map.entry("poneyland").insert_entry("hoho".to_string());
    
    assert_eq!(entry.key(), &"poneyland");
}
