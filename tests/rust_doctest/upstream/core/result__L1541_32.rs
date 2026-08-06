// Extracted from library/core/src/result.rs:1541
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    assert_eq!(unsafe { x.unwrap_unchecked() }, 2);
}
