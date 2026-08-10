// Extracted from library/std/src/collections/hash/set.rs:191
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let mut set = HashSet::new();
    set.insert("a");
    set.insert("b");

    // Will print in an arbitrary order.
    for x in set.iter() {
        println!("{x}");
    }
}
