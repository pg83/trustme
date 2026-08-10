// Extracted from library/core/src/num/mod.rs:1020
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
