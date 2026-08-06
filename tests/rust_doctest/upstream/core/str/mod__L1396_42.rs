// Extracted from library/core/src/str/mod.rs:1396
#![allow(unused)]
fn main() {
    let bananas = "bananas";
    
    assert!(bananas.ends_with("anas"));
    assert!(!bananas.ends_with("nana"));
}
