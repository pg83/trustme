// Extracted from library/core/src/num/mod.rs:824
#![allow(unused)]
#![feature(is_ascii_octdigit)]
fn main() {

    let uppercase_a = b'A';
    let a = b'a';
    let zero = b'0';
    let seven = b'7';
    let nine = b'9';
    let percent = b'%';
    let lf = b'\n';

    assert!(!uppercase_a.is_ascii_octdigit());
    assert!(!a.is_ascii_octdigit());
    assert!(zero.is_ascii_octdigit());
    assert!(seven.is_ascii_octdigit());
    assert!(!nine.is_ascii_octdigit());
    assert!(!percent.is_ascii_octdigit());
    assert!(!lf.is_ascii_octdigit());
}
