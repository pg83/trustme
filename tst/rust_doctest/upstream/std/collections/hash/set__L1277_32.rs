// Extracted from library/std/src/collections/hash/set.rs:1277
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let a = HashSet::from([1, 2, 3]);
    let b = HashSet::from([3, 4, 5]);

    let set = &a - &b;

    let mut i = 0;
    let expected = [1, 2];
    for x in &set {
        assert!(expected.contains(x));
        i += 1;
    }
    assert_eq!(i, expected.len());
}
