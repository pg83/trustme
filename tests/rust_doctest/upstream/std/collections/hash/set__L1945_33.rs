// Extracted from library/std/src/collections/hash/set.rs:1945
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::collections::hash_set::HashSet;
        
        let mut set = HashSet::new();
        set.extend(["a", "b", "c"]);
        assert_eq!(set.len(), 3);
        
        // Existing value (insert)
        let entry = set.entry("a");
        let _raw_o = entry.insert();
        assert_eq!(set.len(), 3);
        // Nonexistent value (insert)
        set.entry("d").insert();
        
        // Existing value (or_insert)
        set.entry("b").or_insert();
        // Nonexistent value (or_insert)
        set.entry("e").or_insert();
        
        println!("Our HashSet: {:?}", set);
        
        let mut vec: Vec<_> = set.iter().copied().collect();
        // The `Iter` iterator produces items in arbitrary order, so the
        // items must be sorted to test them against a sorted array.
        vec.sort_unstable();
        assert_eq!(vec, ["a", "b", "c", "d", "e"]);
        Ok(())
    }
    doctest().unwrap();
}
