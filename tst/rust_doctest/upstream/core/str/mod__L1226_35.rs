// Extracted from library/core/src/str/mod.rs:1226
#![allow(unused)]
fn main() {
    assert_eq!("".split_ascii_whitespace().next(), None);
    assert_eq!("   ".split_ascii_whitespace().next(), None);
}
