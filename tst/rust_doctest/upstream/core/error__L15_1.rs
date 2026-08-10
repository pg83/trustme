// Extracted from library/core/src/error.rs:15
#![allow(unused)]
fn main() {
    let err = "NaN".parse::<u32>().unwrap_err();
    assert_eq!(err.to_string(), "invalid digit found in string");
}
