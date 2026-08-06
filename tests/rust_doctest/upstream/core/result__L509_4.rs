// Extracted from library/core/src/result.rs:509
#![allow(unused)]
fn main() {
    let v = [Ok(2), Ok(4), Err("err!"), Ok(8)];
    let res: Result<Vec<_>, &str> = v.into_iter().collect();
    assert_eq!(res, Err("err!"));
    let v = [Ok(2), Ok(4), Ok(8)];
    let res: Result<Vec<_>, &str> = v.into_iter().collect();
    assert_eq!(res, Ok(vec![2, 4, 8]));
}
