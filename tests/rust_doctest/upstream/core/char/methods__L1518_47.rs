// Extracted from library/core/src/char/methods.rs:1518
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
    
    assert!(!uppercase_a.is_ascii_digit());
    assert!(!uppercase_g.is_ascii_digit());
    assert!(!a.is_ascii_digit());
    assert!(!g.is_ascii_digit());
    assert!(zero.is_ascii_digit());
    assert!(!percent.is_ascii_digit());
    assert!(!space.is_ascii_digit());
    assert!(!lf.is_ascii_digit());
    assert!(!esc.is_ascii_digit());
}
