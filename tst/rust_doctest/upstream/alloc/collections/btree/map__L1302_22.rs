// Extracted from library/alloc/src/collections/btree/map.rs:1302
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeMap;

    let mut count: BTreeMap<&str, usize> = BTreeMap::new();

    // count the number of occurrences of letters in the vec
    for x in ["a", "b", "a", "c", "a", "b"] {
        count.entry(x).and_modify(|curr| *curr += 1).or_insert(1);
    }

    assert_eq!(count["a"], 3);
    assert_eq!(count["b"], 2);
    assert_eq!(count["c"], 1);
}
