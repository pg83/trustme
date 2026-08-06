// Extracted from library/core/src/result.rs:1742
#![allow(unused)]
fn main() {
    let x: Result<Result<Result<&'static str, u32>, u32>, u32> = Ok(Ok(Ok("hello")));
    assert_eq!(Ok(Ok("hello")), x.flatten());
    assert_eq!(Ok("hello"), x.flatten().flatten());
}
