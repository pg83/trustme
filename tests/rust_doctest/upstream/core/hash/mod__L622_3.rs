// Extracted from library/core/src/hash/mod.rs:622
#![allow(unused)]
fn main() {
    use std::hash::{BuildHasher, Hasher, RandomState};
    
    let s = RandomState::new();
    let mut hasher_1 = s.build_hasher();
    let mut hasher_2 = s.build_hasher();
    
    hasher_1.write_u32(8128);
    hasher_2.write_u32(8128);
    
    assert_eq!(hasher_1.finish(), hasher_2.finish());
}
