// Extracted from library/core/src/iter/traits/iterator.rs:3599
#![allow(unused)]
fn main() {
    fn factorial(n: u32) -> u32 {
        (1..=n).product()
    }
    assert_eq!(factorial(0), 1);
    assert_eq!(factorial(1), 1);
    assert_eq!(factorial(5), 120);
}
