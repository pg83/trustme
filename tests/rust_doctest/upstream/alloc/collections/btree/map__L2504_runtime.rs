// Extracted from library/alloc/src/collections/btree/map.rs:2504
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    
    let mut map = BTreeMap::from([
       ("a", 1),
       ("b", 2),
       ("c", 3),
    ]);
    
    // add 10 to the value if the key isn't "a"
    for (key, value) in map.iter_mut() {
        if key != &"a" {
            *value += 10;
        }
    }
}
