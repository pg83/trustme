// Extracted from library/core/src/char/methods.rs:1484
#![allow(unused)]
fn main() {
    let uppercase_a = 'A';
    let uppercase_g = 'G';
    let a = 'a';
    let g = 'g';
    let zero = '0';
    let percent = '%';
    let space = ' ';
    let lf = '\n';
    let esc = '\x1b';

    assert!(uppercase_a.is_ascii_alphanumeric());
    assert!(uppercase_g.is_ascii_alphanumeric());
    assert!(a.is_ascii_alphanumeric());
    assert!(g.is_ascii_alphanumeric());
    assert!(zero.is_ascii_alphanumeric());
    assert!(!percent.is_ascii_alphanumeric());
    assert!(!space.is_ascii_alphanumeric());
    assert!(!lf.is_ascii_alphanumeric());
    assert!(!esc.is_ascii_alphanumeric());
}
