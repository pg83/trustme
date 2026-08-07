// Extracted from library/core/src/str/mod.rs:1333
#![allow(unused)]
fn main() {
    let bananas = "bananas";

    assert!(bananas.contains("nana"));
    assert!(!bananas.contains("apples"));
}
