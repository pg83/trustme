// Extracted from library/core/src/num/mod.rs:651
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
