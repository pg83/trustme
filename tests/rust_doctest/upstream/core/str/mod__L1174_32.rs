// Extracted from library/core/src/str/mod.rs:1174
#![allow(unused)]
fn main() {
    assert_eq!("".split_whitespace().next(), None);
    assert_eq!("   ".split_whitespace().next(), None);
}
