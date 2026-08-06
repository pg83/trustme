// Extracted from library/core/src/slice/mod.rs:1753
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let mut iter = slice.rchunks_exact(2);
    assert_eq!(iter.next().unwrap(), &['e', 'm']);
    assert_eq!(iter.next().unwrap(), &['o', 'r']);
    assert!(iter.next().is_none());
    assert_eq!(iter.remainder(), &['l']);
}
