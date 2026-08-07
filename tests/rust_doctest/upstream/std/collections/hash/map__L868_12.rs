// Extracted from library/std/src/collections/hash/map.rs:868
#![allow(unused)]
fn main() {
    use std::collections::HashMap;

    let mut letters = HashMap::new();

    for ch in "a short treatise on fungi".chars() {
        letters.entry(ch).and_modify(|counter| *counter += 1).or_insert(1);
    }

    assert_eq!(letters[&'s'], 2);
    assert_eq!(letters[&'t'], 3);
    assert_eq!(letters[&'u'], 1);
    assert_eq!(letters.get(&'y'), None);
}
