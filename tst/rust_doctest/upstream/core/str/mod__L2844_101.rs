// Extracted from library/core/src/str/mod.rs:2844
#![allow(unused)]
fn main() {
    assert_eq!(" \t \u{3000}hello world\n".trim_ascii_start(), "\u{3000}hello world\n");
    assert_eq!("  ".trim_ascii_start(), "");
    assert_eq!("".trim_ascii_start(), "");
}
