// Extracted from library/core/src/str/mod.rs:2164
#![allow(unused)]
fn main() {
    let s = "\n Hello\tworld\t\n";
    assert_eq!("Hello\tworld\t\n", s.trim_start());
}
