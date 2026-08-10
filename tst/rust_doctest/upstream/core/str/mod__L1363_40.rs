// Extracted from library/core/src/str/mod.rs:1363
#![allow(unused)]
fn main() {
    let bananas = "bananas";

    assert!(bananas.starts_with("bana"));
    assert!(!bananas.starts_with("nana"));
}
