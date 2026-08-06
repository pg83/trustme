// Extracted from library/core/src/str/mod.rs:2942
#![allow(unused)]
fn main() {
    assert_eq!("❤\n!".escape_debug().to_string(), "❤\\n!");
}
