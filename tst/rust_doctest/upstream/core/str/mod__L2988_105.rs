// Extracted from library/core/src/str/mod.rs:2988
#![allow(unused)]
fn main() {
    assert_eq!("❤\n!".escape_default().to_string(), "\\u{2764}\\n!");
}
