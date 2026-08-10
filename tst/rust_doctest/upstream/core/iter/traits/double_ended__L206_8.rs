// Extracted from library/core/src/iter/traits/double_ended.rs:206
#![allow(unused)]
fn main() {
    let a = ["1", "2", "3"];
    let sum = a.iter()
        .map(|&s| s.parse::<i32>())
        .try_rfold(0, |acc, x| x.and_then(|y| Ok(acc + y)));
    assert_eq!(sum, Ok(6));
}
