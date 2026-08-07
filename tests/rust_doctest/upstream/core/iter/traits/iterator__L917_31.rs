// Extracted from library/core/src/iter/traits/iterator.rs:917
#![allow(unused)]
fn main() {
    let a = ["1", "two", "NaN", "four", "5"];

    let mut iter = a.iter().filter_map(|s| s.parse().ok());

    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(5));
    assert_eq!(iter.next(), None);
}
