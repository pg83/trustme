// Extracted from library/alloc/src/collections/btree/set/entry.rs:18
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::collections::btree_set::BTreeSet;
        
        let mut set = BTreeSet::new();
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
        
        println!("Our BTreeSet: {:?}", set);
        assert!(set.iter().eq(&["a", "b", "c", "d", "e"]));
        Ok(())
    }
    doctest().unwrap();
}
