// Extracted from library/core/src/hash/mod.rs:221
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hash, Hasher};

    let mut hasher = DefaultHasher::new();
    let numbers = [6, 28, 496, 8128];
    Hash::hash_slice(&numbers, &mut hasher);
    println!("Hash is {:x}!", hasher.finish());
}
