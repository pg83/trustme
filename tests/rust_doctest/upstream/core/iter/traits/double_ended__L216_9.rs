// Extracted from library/core/src/iter/traits/double_ended.rs:216
#![allow(unused)]
fn main() {
    let a = ["1", "rust", "3"];
    let mut it = a.iter();
    let sum = it
        .by_ref()
        .map(|&s| s.parse::<i32>())
        .try_rfold(0, |acc, x| x.and_then(|y| Ok(acc + y)));
    assert!(sum.is_err());

    // Because it short-circuited, the remaining elements are still
    // available through the iterator.
    assert_eq!(it.next_back(), Some(&"1"));
}
