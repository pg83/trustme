// Extracted from library/core/src/hash/mod.rs:324
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hasher};
    
    let mut hasher = DefaultHasher::new();
    hasher.write(b"Cool!");
    
    println!("Hash is {:x}!", hasher.finish());
}
