// Extracted from library/core/src/result.rs:1431
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    let y: Result<u32, &str> = Err("late error");
    assert_eq!(x.or(y), Ok(2));
    
    let x: Result<u32, &str> = Err("early error");
    let y: Result<u32, &str> = Ok(2);
    assert_eq!(x.or(y), Ok(2));
    
    let x: Result<u32, &str> = Err("not a 2");
    let y: Result<u32, &str> = Err("late error");
    assert_eq!(x.or(y), Err("late error"));
    
    let x: Result<u32, &str> = Ok(2);
    let y: Result<u32, &str> = Ok(100);
    assert_eq!(x.or(y), Ok(2));
}
