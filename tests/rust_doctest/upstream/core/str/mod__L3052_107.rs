// Extracted from library/core/src/str/mod.rs:3052
#![allow(unused)]
#![feature(substr_range)]
fn main() {
    
    let data = "a, b, b, a";
    let mut iter = data.split(", ").map(|s| data.substr_range(s).unwrap());
    
    assert_eq!(iter.next(), Some(0..1));
    assert_eq!(iter.next(), Some(3..4));
    assert_eq!(iter.next(), Some(6..7));
    assert_eq!(iter.next(), Some(9..10));
}
