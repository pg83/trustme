// Extracted from library/core/src/str/mod.rs:2695
#![allow(unused)]
fn main() {
    let nope = "j".parse::<u32>();
    
    assert!(nope.is_err());
}
