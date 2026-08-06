// Extracted from library/core/src/cmp.rs:805
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    struct Character {
        health: u32,
        experience: u32,
        mana: f32,
    }
    
    impl Ord for Character {
        fn cmp(&self, other: &Self) -> Ordering {
            self.experience
                .cmp(&other.experience)
                .then(self.health.cmp(&other.health))
        }
    }
    
    impl PartialOrd for Character {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.cmp(other))
        }
    }
    
    impl PartialEq for Character {
        fn eq(&self, other: &Self) -> bool {
            self.health == other.health && self.experience == other.experience
        }
    }
    
    impl Eq for Character {}
}
