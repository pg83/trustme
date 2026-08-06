// Extracted from library/std/src/collections/hash/map.rs:779
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    let mut map: HashMap<&str, i32> = HashMap::new();
    map.reserve(10);
}
