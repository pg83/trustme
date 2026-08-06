// Extracted from library/core/src/iter/traits/accum.rs:258
#![allow(unused)]
fn main() {
    let nums = vec!["5", "10", "1", "2"];
    let total: Option<usize> = nums.iter().map(|w| w.parse::<usize>().ok()).product();
    assert_eq!(total, Some(100));
    let nums = vec!["5", "10", "one", "2"];
    let total: Option<usize> = nums.iter().map(|w| w.parse::<usize>().ok()).product();
    assert_eq!(total, None);
}
