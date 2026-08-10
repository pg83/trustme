// Extracted from library/core/src/option.rs:469
#![allow(unused)]
fn main() {
    let v = [Some(2), Some(4), None, Some(8)];
    let res: Option<Vec<_>> = v.into_iter().collect();
    assert_eq!(res, None);
    let v = [Some(2), Some(4), Some(8)];
    let res: Option<Vec<_>> = v.into_iter().collect();
    assert_eq!(res, Some(vec![2, 4, 8]));
}
