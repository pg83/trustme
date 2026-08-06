// Extracted from library/core/src/char/methods.rs:667
#![allow(unused)]
fn main() {
    let n = 'ß'.len_utf16();
    assert_eq!(n, 1);
    
    let len = '💣'.len_utf16();
    assert_eq!(len, 2);
}
