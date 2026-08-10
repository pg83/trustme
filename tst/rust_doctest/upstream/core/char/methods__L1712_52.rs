// Extracted from library/core/src/char/methods.rs:1712
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
