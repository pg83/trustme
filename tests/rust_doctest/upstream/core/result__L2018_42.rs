// Extracted from library/core/src/result.rs:2018
#![allow(unused)]
fn main() {
    let v = vec![1, 2];
    let res: Result<Vec<u32>, &'static str> = v.iter().map(|x: &u32|
        x.checked_add(1).ok_or("Overflow!")
    ).collect();
    assert_eq!(res, Ok(vec![2, 3]));
}
