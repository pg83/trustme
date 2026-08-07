// Extracted from library/core/src/cmp.rs:842
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    #[derive(Debug)]
    struct Character {
        health: f32,
    }

    impl Ord for Character {
        fn cmp(&self, other: &Self) -> std::cmp::Ordering {
            if self.health < other.health {
                Ordering::Less
            } else if self.health > other.health {
                Ordering::Greater
            } else {
                Ordering::Equal
            }
        }
    }

    impl PartialOrd for Character {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.cmp(other))
        }
    }

    impl PartialEq for Character {
        fn eq(&self, other: &Self) -> bool {
            self.health == other.health
        }
    }

    impl Eq for Character {}

    let a = Character { health: 4.5 };
    let b = Character { health: f32::NAN };

    // Mistake: floating-point values do not form a total order and using the built-in comparison
    // operands to implement `Ord` irregardless of that reality does not change it. Use
    // `f32::total_cmp` if you need a total order for floating-point values.

    // Reflexivity requirement of `Ord` is not given.
    assert!(a == a);
    assert!(b != b);

    // Antisymmetry requirement of `Ord` is not given. Only one of a < c and c < a is allowed to be
    // true, not both or neither.
    assert_eq!((a < b) as u8 + (b < a) as u8, 0);
}
