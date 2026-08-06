// Extracted from library/core/src/str/mod.rs:2210
#![allow(unused)]
fn main() {
    let s = "  English  ";
    assert!(Some('h') == s.trim_end().chars().rev().next());
    
    let s = "  עברית  ";
    assert!(Some('ת') == s.trim_end().chars().rev().next());
}
