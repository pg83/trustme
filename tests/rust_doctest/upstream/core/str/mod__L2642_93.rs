// Extracted from library/core/src/str/mod.rs:2642
#![allow(unused)]
fn main() {
    assert_eq!("1fooX".trim_right_matches(|c| c == '1' || c == 'X'), "1foo");
}
