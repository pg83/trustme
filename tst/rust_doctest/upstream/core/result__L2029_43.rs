// Extracted from library/core/src/result.rs:2029
#![allow(unused)]
fn main() {
    let v = vec![1, 2, 0];
    let res: Result<Vec<u32>, &'static str> = v.iter().map(|x: &u32|
        x.checked_sub(1).ok_or("Underflow!")
    ).collect();
    assert_eq!(res, Err("Underflow!"));
}
