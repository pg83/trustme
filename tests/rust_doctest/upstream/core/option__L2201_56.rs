// Extracted from library/core/src/option.rs:2201
#![allow(unused)]
fn main() {
    let x = Some("string");
    let v: Vec<&str> = x.into_iter().collect();
    assert_eq!(v, ["string"]);
    
    let x = None;
    let v: Vec<&str> = x.into_iter().collect();
    assert!(v.is_empty());
}
