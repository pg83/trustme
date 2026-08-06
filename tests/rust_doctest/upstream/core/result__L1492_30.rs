// Extracted from library/core/src/result.rs:1492
#![allow(unused)]
fn main() {
    let default = 2;
    let x: Result<u32, &str> = Ok(9);
    assert_eq!(x.unwrap_or(default), 9);
    
    let x: Result<u32, &str> = Err("error");
    assert_eq!(x.unwrap_or(default), default);
}
