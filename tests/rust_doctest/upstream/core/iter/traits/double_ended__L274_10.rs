// Extracted from library/core/src/iter/traits/double_ended.rs:274
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    // the sum of all of the elements of a
    let sum = a.iter()
               .rfold(0, |acc, &x| acc + x);

    assert_eq!(sum, 6);
}
