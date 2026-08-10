// Extracted from library/core/src/option.rs:997
#![allow(unused)]
fn main() {
    let x: Option<&str> = None;
    assert_eq!(x.unwrap(), "air"); // fails
}
