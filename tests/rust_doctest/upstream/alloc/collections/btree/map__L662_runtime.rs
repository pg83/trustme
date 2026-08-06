// Extracted from library/alloc/src/collections/btree/map.rs:662
#![allow(unused)]
#![feature(allocator_api)]
#![feature(btreemap_alloc)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;
    use std::alloc::Global;
    
    let mut map = BTreeMap::new_in(Global);
    
    // entries can now be inserted into the empty map
    map.insert(1, "a");
}
