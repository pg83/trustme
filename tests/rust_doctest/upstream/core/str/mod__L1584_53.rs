// Extracted from library/core/src/str/mod.rs:1584
#![allow(unused)]
fn main() {
    let x = "(///)".to_string();
    let d: Vec<_> = x.split('/').collect();
    
    assert_eq!(d, &["(", "", "", ")"]);
}
