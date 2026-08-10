// Extracted from library/core/src/ops/bit.rs:9
#![allow(unused)]
fn main() {
    use std::ops::Not;

    #[derive(Debug, PartialEq)]
    enum Answer {
        Yes,
        No,
    }

    impl Not for Answer {
        type Output = Self;

        fn not(self) -> Self::Output {
            match self {
                Answer::Yes => Answer::No,
                Answer::No => Answer::Yes
            }
        }
    }

    assert_eq!(!Answer::Yes, Answer::No);
    assert_eq!(!Answer::No, Answer::Yes);
}
