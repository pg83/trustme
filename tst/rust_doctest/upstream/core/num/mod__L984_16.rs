// Extracted from library/core/src/num/mod.rs:984
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

    assert!(!uppercase_a.is_ascii_whitespace());
    assert!(!uppercase_g.is_ascii_whitespace());
    assert!(!a.is_ascii_whitespace());
    assert!(!g.is_ascii_whitespace());
    assert!(!zero.is_ascii_whitespace());
    assert!(!percent.is_ascii_whitespace());
    assert!(space.is_ascii_whitespace());
    assert!(lf.is_ascii_whitespace());
    assert!(!esc.is_ascii_whitespace());
}
