// Extracted from library/core/src/cmp.rs:1267
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    struct Person {
        id: u32,
        name: String,
        height: f64,
    }
    
    impl PartialOrd for Person {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            self.height.partial_cmp(&other.height)
        }
    }
    
    impl PartialEq for Person {
        fn eq(&self, other: &Self) -> bool {
            self.height == other.height
        }
    }
}
