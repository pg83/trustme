// Extracted from library/std/src/collections/hash/set.rs:2229
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    use std::collections::hash_set::Entry;
    
    let mut set = HashSet::new();
    // The set is empty
    assert!(set.is_empty() && set.capacity() == 0);
    
    set.entry("poneyland").or_insert();
    let capacity_before_remove = set.capacity();
    
    if let Entry::Occupied(o) = set.entry("poneyland") {
        assert_eq!(o.remove(), "poneyland");
    }
    
    assert_eq!(set.contains("poneyland"), false);
    // Now set hold none elements but capacity is equal to the old one
    assert!(set.len() == 0 && set.capacity() == capacity_before_remove);
}
