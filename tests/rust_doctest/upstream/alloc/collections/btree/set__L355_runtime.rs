// Extracted from library/alloc/src/collections/btree/set.rs:355
#![allow(unused)]
#![allow(unused_mut)]
#![feature(allocator_api)]
#![feature(btreemap_alloc)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    use std::alloc::Global;

    let mut set: BTreeSet<i32> = BTreeSet::new_in(Global);
}
