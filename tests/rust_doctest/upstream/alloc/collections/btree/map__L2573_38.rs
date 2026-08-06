// Extracted from library/alloc/src/collections/btree/map.rs:2573
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut a = BTreeMap::new();
    a.insert(1, String::from("hello"));
    a.insert(2, String::from("goodbye"));
    
    for value in a.values_mut() {
        value.push_str("!");
    }
    
    let values: Vec<String> = a.values().cloned().collect();
    assert_eq!(values, [String::from("hello!"),
                        String::from("goodbye!")]);
}
