// Extracted from library/core/src/result.rs:1729
#![allow(unused)]
fn main() {
    let x: Result<Result<&'static str, u32>, u32> = Ok(Ok("hello"));
    assert_eq!(Ok("hello"), x.flatten());
    
    let x: Result<Result<&'static str, u32>, u32> = Ok(Err(6));
    assert_eq!(Err(6), x.flatten());
    
    let x: Result<Result<&'static str, u32>, u32> = Err(6);
    assert_eq!(Err(6), x.flatten());
}
