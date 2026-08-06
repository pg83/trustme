// Extracted from library/std/src/path.rs:248
#![allow(unused)]
fn main() {
    use std::path;
    
    assert!(path::is_separator('/')); // '/' works for both Unix and Windows
    assert!(!path::is_separator('❤'));
}
