// Extracted from src/expressions.md:132
#![allow(unused)]
fn main() {
    // Using vec instead of array to avoid references
    // since there is no stable owned array iterator
    // at the time this example was written.
    let mut one_two = vec![1, 2].into_iter();
    assert_eq!(
        (1, 2),
        (one_two.next().unwrap(), one_two.next().unwrap())
    );
}
