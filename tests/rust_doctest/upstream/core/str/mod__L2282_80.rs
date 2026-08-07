// Extracted from library/core/src/str/mod.rs:2282
#![allow(unused)]
fn main() {
    let s = " Hello\tworld\t";

    assert_eq!(" Hello\tworld", s.trim_right());
}
