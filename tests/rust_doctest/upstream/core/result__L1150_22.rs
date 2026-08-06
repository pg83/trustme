// Extracted from library/core/src/result.rs:1150
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    assert_eq!(x.unwrap(), 2);
}
