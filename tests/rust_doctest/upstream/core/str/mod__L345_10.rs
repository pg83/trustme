// Extracted from library/core/src/str/mod.rs:345
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";
    assert!(s.is_char_boundary(0));
    // start of `老`
    assert!(s.is_char_boundary(6));
    assert!(s.is_char_boundary(s.len()));
    
    // second byte of `ö`
    assert!(!s.is_char_boundary(2));
    
    // third byte of `老`
    assert!(!s.is_char_boundary(8));
}
