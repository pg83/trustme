// Extracted from library/core/src/str/mod.rs:2687
#![allow(unused)]
fn main() {
    let four = "4".parse::<u32>();
    
    assert_eq!(Ok(4), four);
}
