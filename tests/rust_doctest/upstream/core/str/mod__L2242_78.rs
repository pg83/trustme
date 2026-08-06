// Extracted from library/core/src/str/mod.rs:2242
#![allow(unused)]
fn main() {
    let s = " Hello\tworld\t";
    
    assert_eq!("Hello\tworld\t", s.trim_left());
}
