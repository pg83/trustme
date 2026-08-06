// Extracted from library/core/src/result.rs:728
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    assert_eq!(x.as_ref(), Ok(&2));
    
    let x: Result<u32, &str> = Err("Error");
    assert_eq!(x.as_ref(), Err(&"Error"));
}
