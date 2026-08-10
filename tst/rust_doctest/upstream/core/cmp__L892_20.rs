// Extracted from library/core/src/cmp.rs:892
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    #[derive(Debug)]
    struct Character {
        health: u32,
        experience: u32,
    }

    impl PartialOrd for Character {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.cmp(other))
        }
    }

    impl Ord for Character {
        fn cmp(&self, other: &Self) -> std::cmp::Ordering {
            if self.health < 50 {
                self.health.cmp(&other.health)
            } else {
                self.experience.cmp(&other.experience)
            }
        }
    }

    // For performance reasons implementing `PartialEq` this way is not the idiomatic way, but it
    // ensures consistent behavior between `PartialEq`, `PartialOrd` and `Ord` in this example.
    impl PartialEq for Character {
        fn eq(&self, other: &Self) -> bool {
            self.cmp(other) == Ordering::Equal
        }
    }

    impl Eq for Character {}

    let a = Character {
        health: 3,
        experience: 5,
    };
    let b = Character {
        health: 10,
        experience: 77,
    };
    let c = Character {
        health: 143,
        experience: 2,
    };

    // Mistake: The implementation of `Ord` compares different fields depending on the value of
    // `self.health`, the resulting order is not total.

    // Transitivity requirement of `Ord` is not given. If a is smaller than b and b is smaller than
    // c, by transitive property a must also be smaller than c.
    assert!(a < b && b < c && c < a);

    // Antisymmetry requirement of `Ord` is not given. Only one of a < c and c < a is allowed to be
    // true, not both or neither.
    assert_eq!((a < c) as u8 + (c < a) as u8, 2);
}
