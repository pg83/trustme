// Extracted from library/core/src/result.rs:2040
#![allow(unused)]
fn main() {
    let v = vec![3, 2, 1, 10];
    let mut shared = 0;
    let res: Result<Vec<u32>, &'static str> = v.iter().map(|x: &u32| {
        shared += x;
        x.checked_sub(2).ok_or("Underflow!")
    }).collect();
    assert_eq!(res, Err("Underflow!"));
    assert_eq!(shared, 6);
}
