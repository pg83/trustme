// Extracted from library/core/src/str/mod.rs:2203
#![allow(unused)]
fn main() {
    let s = "\n Hello\tworld\t\n";
    assert_eq!("\n Hello\tworld", s.trim_end());
}
