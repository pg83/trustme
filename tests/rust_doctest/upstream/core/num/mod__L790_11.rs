// Extracted from library/core/src/num/mod.rs:790
#![allow(unused)]
fn main() {
    let uppercase_a = b'A';
    let uppercase_g = b'G';
    let a = b'a';
    let g = b'g';
    let zero = b'0';
    let percent = b'%';
    let space = b' ';
    let lf = b'\n';
    let esc = b'\x1b';

    assert!(!uppercase_a.is_ascii_digit());
    assert!(!uppercase_g.is_ascii_digit());
    assert!(!a.is_ascii_digit());
    assert!(!g.is_ascii_digit());
    assert!(zero.is_ascii_digit());
    assert!(!percent.is_ascii_digit());
    assert!(!space.is_ascii_digit());
    assert!(!lf.is_ascii_digit());
    assert!(!esc.is_ascii_digit());
}
