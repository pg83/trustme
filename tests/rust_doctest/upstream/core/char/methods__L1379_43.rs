// Extracted from library/core/src/char/methods.rs:1379
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

    assert!(uppercase_a.is_ascii_alphabetic());
    assert!(uppercase_g.is_ascii_alphabetic());
    assert!(a.is_ascii_alphabetic());
    assert!(g.is_ascii_alphabetic());
    assert!(!zero.is_ascii_alphabetic());
    assert!(!percent.is_ascii_alphabetic());
    assert!(!space.is_ascii_alphabetic());
    assert!(!lf.is_ascii_alphabetic());
    assert!(!esc.is_ascii_alphabetic());
}
