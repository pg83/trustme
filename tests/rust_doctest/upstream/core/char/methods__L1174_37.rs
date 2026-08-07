// Extracted from library/core/src/char/methods.rs:1174
#![allow(unused)]
fn main() {
    let ascii = 'a';
    let non_ascii = '❤';

    assert!(ascii.is_ascii());
    assert!(!non_ascii.is_ascii());
}
