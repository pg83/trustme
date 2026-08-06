// Extracted from library/alloc/src/collections/btree/set.rs:967
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    
    use std::collections::BTreeSet;
    
    let mut set: BTreeSet<String> = ["cat", "dog", "horse"]
        .iter().map(|&pet| pet.to_owned()).collect();
    
    assert_eq!(set.len(), 3);
    for &pet in &["cat", "dog", "fish"] {
        let value = set.get_or_insert_with(pet, str::to_owned);
        assert_eq!(value, pet);
    }
    assert_eq!(set.len(), 4); // a new "fish" was inserted
}
