// Extracted from library/core/src/result.rs:1219
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(10);
    x.expect_err("Testing expect_err"); // panics with `Testing expect_err: 10`
}
