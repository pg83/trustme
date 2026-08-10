// Extracted from library/alloc/src/collections/btree/map.rs:1272
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut map: BTreeMap<&str, i32> =
        [("Alice", 0), ("Bob", 0), ("Carol", 0), ("Cheryl", 0)].into();
    for (_, balance) in map.range_mut("B".."Cheryl") {
        *balance += 100;
    }
    for (name, balance) in &map {
        println!("{name} => {balance}");
    }
}
