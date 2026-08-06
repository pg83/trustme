// Extracted from library/core/src/iter/traits/accum.rs:167
#![allow(unused)]
fn main() {
    let f = |&x: &i32| if x < 0 { Err("Negative element found") } else { Ok(x) };
    let v = vec![1, 2];
    let res: Result<i32, _> = v.iter().map(f).sum();
    assert_eq!(res, Ok(3));
    let v = vec![1, -2];
    let res: Result<i32, _> = v.iter().map(f).sum();
    assert_eq!(res, Err("Negative element found"));
}
