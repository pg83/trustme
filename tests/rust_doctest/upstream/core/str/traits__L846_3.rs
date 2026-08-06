// Extracted from library/core/src/str/traits.rs:846
#![allow(unused)]
fn main() {
    use std::str::FromStr;
    
    let s = "5";
    let x = i32::from_str(s).unwrap();
    
    assert_eq!(5, x);
}
