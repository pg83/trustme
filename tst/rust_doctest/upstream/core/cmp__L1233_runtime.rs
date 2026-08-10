// Extracted from library/core/src/cmp.rs:1233
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    struct Person {
        id: u32,
        name: String,
        height: u32,
    }

    impl PartialOrd for Person {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.cmp(other))
        }
    }

    impl Ord for Person {
        fn cmp(&self, other: &Self) -> Ordering {
            self.height.cmp(&other.height)
        }
    }

    impl PartialEq for Person {
        fn eq(&self, other: &Self) -> bool {
            self.height == other.height
        }
    }

    impl Eq for Person {}
}
