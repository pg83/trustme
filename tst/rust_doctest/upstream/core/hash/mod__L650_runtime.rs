// Extracted from library/core/src/hash/mod.rs:650
#![allow(unused)]
fn main() {
    use std::hash::{BuildHasher, RandomState};

    let s = RandomState::new();
    let new_s = s.build_hasher();
}
