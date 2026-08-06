// Extracted from library/core/src/hash/mod.rs:723
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::hash::{BuildHasherDefault, Hasher};
    
    #[derive(Default)]
    struct MyHasher;
    
    impl Hasher for MyHasher {
        fn write(&mut self, bytes: &[u8]) {
            // Your hashing algorithm goes here!
           unimplemented!()
        }
    
        fn finish(&self) -> u64 {
            // Your hashing algorithm goes here!
            unimplemented!()
        }
    }
    
    type MyBuildHasher = BuildHasherDefault<MyHasher>;
    
    let hash_map = HashMap::<u32, u32, MyBuildHasher>::default();
}
