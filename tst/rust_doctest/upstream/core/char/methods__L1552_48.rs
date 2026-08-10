// Extracted from library/core/src/char/methods.rs:1552
#![allow(unused)]
#![feature(is_ascii_octdigit)]
fn main() {

    let uppercase_a = 'A';
    let a = 'a';
    let zero = '0';
    let seven = '7';
    let nine = '9';
    let percent = '%';
    let lf = '\n';

    assert!(!uppercase_a.is_ascii_octdigit());
    assert!(!a.is_ascii_octdigit());
    assert!(zero.is_ascii_octdigit());
    assert!(seven.is_ascii_octdigit());
    assert!(!nine.is_ascii_octdigit());
    assert!(!percent.is_ascii_octdigit());
    assert!(!lf.is_ascii_octdigit());
}
