// Extracted from library/core/src/str/mod.rs:1603
#![allow(unused)]
fn main() {
    let f: Vec<_> = "rust".split("").collect();
    assert_eq!(f, &["", "r", "u", "s", "t", ""]);
}
