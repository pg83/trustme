// Extracted from library/core/src/num/mod.rs:756
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
