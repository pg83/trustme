// Extracted from library/std/src/collections/hash/set.rs:783
#![allow(unused)]
#![feature(hash_set_entry)]
fn main() {
    
    use std::collections::HashSet;
    use std::collections::hash_set::Entry::*;
    
    let mut singles = HashSet::new();
    let mut dupes = HashSet::new();
    
    for ch in "a short treatise on fungi".chars() {
        if let Vacant(dupe_entry) = dupes.entry(ch) {
            // We haven't already seen a duplicate, so
            // check if we've at least seen it once.
            match singles.entry(ch) {
                Vacant(single_entry) => {
                    // We found a new character for the first time.
                    single_entry.insert()
                }
                Occupied(single_entry) => {
                    // We've already seen this once, "move" it to dupes.
                    single_entry.remove();
                    dupe_entry.insert();
                }
            }
        }
    }
    
    assert!(!singles.contains(&'t') && dupes.contains(&'t'));
    assert!(singles.contains(&'u') && !dupes.contains(&'u'));
    assert!(!singles.contains(&'v') && !dupes.contains(&'v'));
}
