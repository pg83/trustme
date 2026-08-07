// Extracted from library/alloc/src/collections/btree/set.rs:997
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;
    use std::collections::btree_set::Entry::*;

    let mut singles = BTreeSet::new();
    let mut dupes = BTreeSet::new();

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
