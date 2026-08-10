// Extracted from library/core/src/hash/mod.rs:296
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hasher};

    let mut hasher = DefaultHasher::new();

    hasher.write_u32(1989);
    hasher.write_u8(11);
    hasher.write_u8(9);
    hasher.write(b"Huh?");

    println!("Hash is {:x}!", hasher.finish());
}
