// Extracted from library/core/src/hash/mod.rs:15
#![allow(unused)]
fn main() {
    use std::hash::{DefaultHasher, Hash, Hasher};
    
    #[derive(Hash)]
    struct Person {
        id: u32,
        name: String,
        phone: u64,
    }
    
    let person1 = Person {
        id: 5,
        name: "Janet".to_string(),
        phone: 555_666_7777,
    };
    let person2 = Person {
        id: 5,
        name: "Bob".to_string(),
        phone: 555_666_7777,
    };
    
    assert!(calculate_hash(&person1) != calculate_hash(&person2));
    
    fn calculate_hash<T: Hash>(t: &T) -> u64 {
        let mut s = DefaultHasher::new();
        t.hash(&mut s);
        s.finish()
    }
}
