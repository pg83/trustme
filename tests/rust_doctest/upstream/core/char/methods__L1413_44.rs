// Extracted from library/core/src/char/methods.rs:1413
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

    assert!(uppercase_a.is_ascii_uppercase());
    assert!(uppercase_g.is_ascii_uppercase());
    assert!(!a.is_ascii_uppercase());
    assert!(!g.is_ascii_uppercase());
    assert!(!zero.is_ascii_uppercase());
    assert!(!percent.is_ascii_uppercase());
    assert!(!space.is_ascii_uppercase());
    assert!(!lf.is_ascii_uppercase());
    assert!(!esc.is_ascii_uppercase());
}
