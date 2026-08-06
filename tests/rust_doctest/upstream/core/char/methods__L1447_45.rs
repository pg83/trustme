// Extracted from library/core/src/char/methods.rs:1447
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
    
    assert!(!uppercase_a.is_ascii_lowercase());
    assert!(!uppercase_g.is_ascii_lowercase());
    assert!(a.is_ascii_lowercase());
    assert!(g.is_ascii_lowercase());
    assert!(!zero.is_ascii_lowercase());
    assert!(!percent.is_ascii_lowercase());
    assert!(!space.is_ascii_lowercase());
    assert!(!lf.is_ascii_lowercase());
    assert!(!esc.is_ascii_lowercase());
}
