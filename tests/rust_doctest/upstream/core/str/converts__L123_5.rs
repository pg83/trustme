// Extracted from library/core/src/str/converts.rs:123
#![allow(unused)]
fn main() {
    use std::str;
    
    // Some invalid bytes in a mutable vector
    let mut invalid = vec![128, 223];
    
    assert!(str::from_utf8_mut(&mut invalid).is_err());
}
