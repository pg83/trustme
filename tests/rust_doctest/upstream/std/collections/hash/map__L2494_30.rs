// Extracted from library/std/src/collections/hash/map.rs:2494
fn main() {
use std::collections::HashMap;

let mut map: HashMap<&str, Option<u32>> = HashMap::new();
map.entry("poneyland").or_default();

assert_eq!(map["poneyland"], None);
}
