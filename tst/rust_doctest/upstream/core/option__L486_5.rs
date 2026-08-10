// Extracted from library/core/src/option.rs:486
#![allow(unused)]
fn main() {
    let v = [None, Some(1), Some(2), Some(3)];
    let res: Option<i32> = v.into_iter().sum();
    assert_eq!(res, None);
    let v = [Some(1), Some(2), Some(21)];
    let res: Option<i32> = v.into_iter().product();
    assert_eq!(res, Some(42));
}
