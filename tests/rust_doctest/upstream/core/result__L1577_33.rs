// Extracted from library/core/src/result.rs:1577
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Err("emergency failure");
    assert_eq!(unsafe { x.unwrap_err_unchecked() }, "emergency failure");
}
