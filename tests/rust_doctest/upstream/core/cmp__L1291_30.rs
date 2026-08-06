// Extracted from library/core/src/cmp.rs:1291
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    #[derive(PartialEq, Debug)]
    struct Character {
        health: u32,
        experience: u32,
    }
    
    impl PartialOrd for Character {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.health.cmp(&other.health))
        }
    }
    
    let a = Character {
        health: 10,
        experience: 5,
    };
    let b = Character {
        health: 10,
        experience: 77,
    };
    
    // Mistake: `PartialEq` and `PartialOrd` disagree with each other.
    
    assert_eq!(a.partial_cmp(&b).unwrap(), Ordering::Equal); // a == b according to `PartialOrd`.
    assert_ne!(a, b); // a != b according to `PartialEq`.
}
