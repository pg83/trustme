// Extracted from library/core/src/option.rs:1234
#![allow(unused)]
fn main() {
    let k = 21;
    
    let x = Some("foo");
    assert_eq!(x.map_or_else(|| 2 * k, |v| v.len()), 3);
    
    let x: Option<&str> = None;
    assert_eq!(x.map_or_else(|| 2 * k, |v| v.len()), 42);
}
