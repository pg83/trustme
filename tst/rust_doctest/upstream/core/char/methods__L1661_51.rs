// Extracted from library/core/src/char/methods.rs:1661
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

    assert!(uppercase_a.is_ascii_graphic());
    assert!(uppercase_g.is_ascii_graphic());
    assert!(a.is_ascii_graphic());
    assert!(g.is_ascii_graphic());
    assert!(zero.is_ascii_graphic());
    assert!(percent.is_ascii_graphic());
    assert!(!space.is_ascii_graphic());
    assert!(!lf.is_ascii_graphic());
    assert!(!esc.is_ascii_graphic());
}
