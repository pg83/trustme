// Extracted from library/core/src/iter/traits/iterator.rs:3964
#![allow(unused)]
fn main() {
    assert!([1, 2, 2, 9].iter().is_sorted_by(|a, b| a <= b));
    assert!(![1, 2, 2, 9].iter().is_sorted_by(|a, b| a < b));

    assert!([0].iter().is_sorted_by(|a, b| true));
    assert!([0].iter().is_sorted_by(|a, b| false));

    assert!(std::iter::empty::<i32>().is_sorted_by(|a, b| false));
    assert!(std::iter::empty::<i32>().is_sorted_by(|a, b| true));
}
