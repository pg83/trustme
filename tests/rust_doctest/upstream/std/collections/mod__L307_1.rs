// Extracted from library/std/src/collections/mod.rs:307
#![allow(unused)]
fn main() {
    use std::collections::btree_map::BTreeMap;

    let mut count = BTreeMap::new();
    let message = "she sells sea shells by the sea shore";

    for c in message.chars() {
        *count.entry(c).or_insert(0) += 1;
    }

    assert_eq!(count.get(&'s'), Some(&8));

    println!("Number of occurrences of each character");
    for (char, count) in &count {
        println!("{char}: {count}");
    }
}
