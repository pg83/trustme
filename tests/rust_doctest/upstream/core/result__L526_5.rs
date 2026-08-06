// Extracted from library/core/src/result.rs:526
#![allow(unused)]
fn main() {
    let v = [Err("error!"), Ok(1), Ok(2), Ok(3), Err("foo")];
    let res: Result<i32, &str> = v.into_iter().sum();
    assert_eq!(res, Err("error!"));
    let v = [Ok(1), Ok(2), Ok(21)];
    let res: Result<i32, &str> = v.into_iter().product();
    assert_eq!(res, Ok(42));
}
