// Extracted from library/core/src/result.rs:1245
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    x.unwrap_err(); // panics with `2`
}
