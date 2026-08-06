// Extracted from library/core/src/result.rs:701
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    assert_eq!(x.err(), None);
    
    let x: Result<u32, &str> = Err("Nothing here");
    assert_eq!(x.err(), Some("Nothing here"));
}
