// Extracted from library/core/src/str/mod.rs:2171
#![allow(unused)]
fn main() {
    let s = "  English  ";
    assert!(Some('E') == s.trim_start().chars().next());

    let s = "  עברית  ";
    assert!(Some('ע') == s.trim_start().chars().next());
}
