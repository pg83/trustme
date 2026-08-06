// Extracted from library/core/src/iter/traits/iterator.rs:929
#![allow(unused)]
fn main() {
    let a = ["1", "two", "NaN", "four", "5"];
    let mut iter = a.iter().map(|s| s.parse()).filter(|s| s.is_ok()).map(|s| s.unwrap());
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(5));
    assert_eq!(iter.next(), None);
}
