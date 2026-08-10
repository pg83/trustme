// Extracted from library/std/src/collections/hash/map.rs:222
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::hash::{BuildHasherDefault, DefaultHasher};
    use std::sync::{LazyLock, Mutex};

    // HashMaps with a fixed, non-random hasher
    const NONRANDOM_EMPTY_MAP: HashMap<String, Vec<i32>, BuildHasherDefault<DefaultHasher>> =
        HashMap::with_hasher(BuildHasherDefault::new());
    static NONRANDOM_MAP: Mutex<HashMap<String, Vec<i32>, BuildHasherDefault<DefaultHasher>>> =
        Mutex::new(HashMap::with_hasher(BuildHasherDefault::new()));

    // HashMaps using LazyLock to retain random seeding
    const RANDOM_EMPTY_MAP: LazyLock<HashMap<String, Vec<i32>>> =
        LazyLock::new(HashMap::new);
    static RANDOM_MAP: LazyLock<Mutex<HashMap<String, Vec<i32>>>> =
        LazyLock::new(|| Mutex::new(HashMap::new()));
}
