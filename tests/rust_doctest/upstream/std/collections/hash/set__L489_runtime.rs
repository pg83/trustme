// Extracted from library/std/src/collections/hash/set.rs:489
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    let mut set: HashSet<i32> = HashSet::new();
    set.try_reserve(10).expect("why is the test harness OOMing on a handful of bytes?");
}
