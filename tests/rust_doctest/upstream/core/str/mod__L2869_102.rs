// Extracted from library/core/src/str/mod.rs:2869
#![allow(unused)]
fn main() {
    assert_eq!("\r hello world\u{3000}\n ".trim_ascii_end(), "\r hello world\u{3000}");
    assert_eq!("  ".trim_ascii_end(), "");
    assert_eq!("".trim_ascii_end(), "");
}
