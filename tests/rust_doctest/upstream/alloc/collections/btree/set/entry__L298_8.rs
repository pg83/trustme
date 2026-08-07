// Extracted from library/alloc/src/collections/btree/set/entry.rs:298
#![allow(unused)]
#![feature(btree_set_entry)]
extern crate alloc;
fn main() {

    use std::collections::BTreeSet;
    use std::collections::btree_set::Entry;

    let mut set = BTreeSet::new();
    set.entry("poneyland").or_insert();

    if let Entry::Occupied(o) = set.entry("poneyland") {
        assert_eq!(o.remove(), "poneyland");
    }

    assert_eq!(set.contains("poneyland"), false);
}
