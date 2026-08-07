// Extracted from library/core/src/num/mod.rs:473
#![allow(unused)]
fn main() {
    let ascii = 97u8;
    let non_ascii = 150u8;

    assert!(ascii.is_ascii());
    assert!(!non_ascii.is_ascii());
}
