// Extracted from library/std/src/collections/hash/set.rs:114
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    use std::hash::{BuildHasherDefault, DefaultHasher};
    use std::sync::Mutex;

    const EMPTY_SET: HashSet<String, BuildHasherDefault<DefaultHasher>> =
        HashSet::with_hasher(BuildHasherDefault::new());
    static SET: Mutex<HashSet<String, BuildHasherDefault<DefaultHasher>>> =
        Mutex::new(HashSet::with_hasher(BuildHasherDefault::new()));
}
