// Extracted from library/core/src/result.rs:1355
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    let y: Result<&str, &str> = Err("late error");
    assert_eq!(x.and(y), Err("late error"));
    
    let x: Result<u32, &str> = Err("early error");
    let y: Result<&str, &str> = Ok("foo");
    assert_eq!(x.and(y), Err("early error"));
    
    let x: Result<u32, &str> = Err("not a 2");
    let y: Result<&str, &str> = Err("late error");
    assert_eq!(x.and(y), Err("not a 2"));
    
    let x: Result<u32, &str> = Ok(2);
    let y: Result<&str, &str> = Ok("different result type");
    assert_eq!(x.and(y), Ok("different result type"));
}
