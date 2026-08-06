// Extracted from library/core/src/result.rs:1828
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(5);
    let v: Vec<u32> = x.into_iter().collect();
    assert_eq!(v, [5]);
    
    let x: Result<u32, &str> = Err("nothing!");
    let v: Vec<u32> = x.into_iter().collect();
    assert_eq!(v, []);
}
