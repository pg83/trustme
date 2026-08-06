// Extracted from library/core/src/result.rs:818
#![allow(unused)]
fn main() {
    let x: Result<_, &str> = Ok("foo");
    assert_eq!(x.map_or(42, |v| v.len()), 3);
    
    let x: Result<&str, _> = Err("bar");
    assert_eq!(x.map_or(42, |v| v.len()), 42);
}
