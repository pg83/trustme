// Extracted from library/std/src/collections/hash/set.rs:751
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    
    let mut set: HashSet<String> = ["cat", "dog", "horse"]
        .iter().map(|&pet| pet.to_owned()).collect();
    
    assert_eq!(set.len(), 3);
    for &pet in &["cat", "dog", "fish"] {
        let value = set.get_or_insert_with(pet, str::to_owned);
        assert_eq!(value, pet);
    }
    assert_eq!(set.len(), 4); // a new "fish" was inserted
}
