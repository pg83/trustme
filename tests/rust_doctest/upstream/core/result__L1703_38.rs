// Extracted from library/core/src/result.rs:1703
#![allow(unused)]
fn main() {
    #[derive(Debug, Eq, PartialEq)]
    struct SomeErr;

    let x: Result<Option<i32>, SomeErr> = Ok(Some(5));
    let y: Option<Result<i32, SomeErr>> = Some(Ok(5));
    assert_eq!(x.transpose(), y);
}
