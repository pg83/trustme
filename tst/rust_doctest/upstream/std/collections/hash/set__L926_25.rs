// Extracted from library/std/src/collections/hash/set.rs:926
#![allow(unused)]
fn main() {
    use std::collections::HashSet;

    let mut set = HashSet::new();
    set.insert(Vec::<i32>::new());

    assert_eq!(set.get(&[][..]).unwrap().capacity(), 0);
    set.replace(Vec::with_capacity(10));
    assert_eq!(set.get(&[][..]).unwrap().capacity(), 10);
}
