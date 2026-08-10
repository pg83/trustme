// Extracted from library/core/src/result.rs:1087
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Err("emergency failure");
    x.expect("Testing expect"); // panics with `Testing expect: emergency failure`
}
