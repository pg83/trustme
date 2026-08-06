// Extracted from library/core/src/option.rs:1347
#![allow(unused)]
fn main() {
    let x = Some("foo");
    assert_eq!(x.ok_or_else(|| 0), Ok("foo"));
    
    let x: Option<&str> = None;
    assert_eq!(x.ok_or_else(|| 0), Err(0));
}
