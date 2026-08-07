// Extracted from library/core/src/result.rs:844
#![allow(unused)]
fn main() {
    let k = 21;

    let x : Result<_, &str> = Ok("foo");
    assert_eq!(x.map_or_else(|e| k * 2, |v| v.len()), 3);

    let x : Result<&str, _> = Err("bar");
    assert_eq!(x.map_or_else(|e| k * 2, |v| v.len()), 42);
}
