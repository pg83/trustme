// Extracted from library/core/src/result.rs:1514
#![allow(unused)]
fn main() {
    fn count(x: &str) -> usize { x.len() }
    
    assert_eq!(Ok(2).unwrap_or_else(count), 2);
    assert_eq!(Err("foo").unwrap_or_else(count), 3);
}
