// Extracted from library/core/src/fmt/mod.rs:1329
#![allow(unused)]
fn main() {
    let x = 42.0; // 42.0 is '4.2e1' in scientific notation
    
    assert_eq!(format!("{x:e}"), "4.2e1");
}
