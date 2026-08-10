// Extracted from library/std/src/collections/hash/map.rs:1276
use std::collections::HashMap;

fn main() {
let mut map = HashMap::new();
map.insert(1, "a");
assert_eq!(map.remove_entry(&1), Some((1, "a")));
assert_eq!(map.remove(&1), None);
}
