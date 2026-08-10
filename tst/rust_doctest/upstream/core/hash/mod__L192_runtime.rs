// Extracted from library/core/src/hash/mod.rs:192
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hash, Hasher};

    let mut hasher = DefaultHasher::new();
    7920.hash(&mut hasher);
    println!("Hash is {:x}!", hasher.finish());
}
