// Extracted from library/core/src/str/mod.rs:2895
#![allow(unused)]
fn main() {
    assert_eq!("\r hello world\n ".trim_ascii(), "hello world");
    assert_eq!("  ".trim_ascii(), "");
    assert_eq!("".trim_ascii(), "");
}
