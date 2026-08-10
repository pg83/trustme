// Extracted from library/core/src/char/methods.rs:1748
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

    assert!(!uppercase_a.is_ascii_control());
    assert!(!uppercase_g.is_ascii_control());
    assert!(!a.is_ascii_control());
    assert!(!g.is_ascii_control());
    assert!(!zero.is_ascii_control());
    assert!(!percent.is_ascii_control());
    assert!(!space.is_ascii_control());
    assert!(lf.is_ascii_control());
    assert!(esc.is_ascii_control());
}
