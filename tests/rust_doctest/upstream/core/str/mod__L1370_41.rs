// Extracted from library/core/src/str/mod.rs:1370
#![allow(unused)]
fn main() {
    let bananas = "bananas";

    // Note that both of these assert successfully.
    assert!(bananas.starts_with(&['b', 'a', 'n', 'a']));
    assert!(bananas.starts_with(&['a', 'b', 'c', 'd']));
}
