// Extracted from library/core/src/result.rs:1155
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Err("emergency failure");
    x.unwrap(); // panics with `emergency failure`
}
