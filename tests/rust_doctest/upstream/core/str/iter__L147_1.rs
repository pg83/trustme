// Extracted from library/core/src/str/iter.rs:147
#![allow(unused)]
fn main() {
    let mut chars = "abc".chars();
    
    assert_eq!(chars.as_str(), "abc");
    chars.next();
    assert_eq!(chars.as_str(), "bc");
    chars.next();
    chars.next();
    assert_eq!(chars.as_str(), "");
}
