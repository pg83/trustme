// Extracted from library/core/src/iter/traits/accum.rs:198
#![allow(unused)]
fn main() {
    let nums = vec!["5", "10", "1", "2"];
    let total: Result<usize, _> = nums.iter().map(|w| w.parse::<usize>()).product();
    assert_eq!(total, Ok(100));
    let nums = vec!["5", "10", "one", "2"];
    let total: Result<usize, _> = nums.iter().map(|w| w.parse::<usize>()).product();
    assert!(total.is_err());
}
