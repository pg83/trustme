// Extracted from library/alloc/src/collections/btree/set.rs:383
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    use std::ops::Bound::Included;
    
    let mut set = BTreeSet::new();
    set.insert(3);
    set.insert(5);
    set.insert(8);
    for &elem in set.range((Included(&4), Included(&8))) {
        println!("{elem}");
    }
    assert_eq!(Some(&5), set.range(4..).next());
}
