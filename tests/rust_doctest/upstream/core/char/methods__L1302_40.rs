// Extracted from library/core/src/char/methods.rs:1302
#![allow(unused)]
fn main() {
    let upper_a = 'A';
    let lower_a = 'a';
    let lower_z = 'z';

    assert!(upper_a.eq_ignore_ascii_case(&lower_a));
    assert!(upper_a.eq_ignore_ascii_case(&upper_a));
    assert!(!upper_a.eq_ignore_ascii_case(&lower_z));
}
