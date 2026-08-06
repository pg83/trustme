// Extracted from library/core/src/str/mod.rs:1594
#![allow(unused)]
fn main() {
    let d: Vec<_> = "010".split("0").collect();
    assert_eq!(d, &["", "1", ""]);
}
