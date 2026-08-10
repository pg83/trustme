// Extracted from library/core/src/str/mod.rs:3026
#![allow(unused)]
fn main() {
    assert_eq!("❤\n!".escape_unicode().to_string(), "\\u{2764}\\u{a}\\u{21}");
}
