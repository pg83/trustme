// Extracted from library/core/src/num/mod.rs:579
#![allow(unused)]
fn main() {
    let lowercase_a = 97u8;
    let uppercase_a = 65u8;

    assert!(lowercase_a.eq_ignore_ascii_case(&uppercase_a));
}
