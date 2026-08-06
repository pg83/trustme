// Extracted from library/core/src/str/mod.rs:150
#![allow(unused)]
fn main() {
    let s = "";
    assert!(s.is_empty());
    
    let s = "not empty";
    assert!(!s.is_empty());
}
