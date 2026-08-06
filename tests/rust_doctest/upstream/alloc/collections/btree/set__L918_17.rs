// Extracted from library/alloc/src/collections/btree/set.rs:918
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    let mut set = BTreeSet::new();
    set.insert(Vec::<i32>::new());
    
    assert_eq!(set.get(&[][..]).unwrap().capacity(), 0);
    set.replace(Vec::with_capacity(10));
    assert_eq!(set.get(&[][..]).unwrap().capacity(), 10);
}
