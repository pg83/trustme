// Extracted from library/std/src/collections/hash/map.rs:1218
#![allow(unused)]
#![feature(map_try_insert)]
fn main() {

    use std::collections::HashMap;

    let mut map = HashMap::new();
    assert_eq!(map.try_insert(37, "a").unwrap(), &"a");

    let err = map.try_insert(37, "b").unwrap_err();
    assert_eq!(err.entry.key(), &37);
    assert_eq!(err.entry.get(), &"a");
    assert_eq!(err.value, "b");
}
