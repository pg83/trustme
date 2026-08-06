// Extracted from library/core/src/str/mod.rs:2710
#![allow(unused)]
fn main() {
    let ascii = "hello!\n";
    let non_ascii = "Grüße, Jürgen ❤";
    
    assert!(ascii.is_ascii());
    assert!(!non_ascii.is_ascii());
}
