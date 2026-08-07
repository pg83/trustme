// Extracted from library/core/src/hash/mod.rs:120
#![allow(unused)]
fn main() {
    use std::hash::{Hash, Hasher};

    struct Person {
        id: u32,
        name: String,
        phone: u64,
    }

    impl Hash for Person {
        fn hash<H: Hasher>(&self, state: &mut H) {
            self.id.hash(state);
            self.phone.hash(state);
        }
    }
}
