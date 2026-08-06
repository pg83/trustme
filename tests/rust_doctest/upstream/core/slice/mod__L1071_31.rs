// Extracted from library/core/src/slice/mod.rs:1071
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let mut iter = slice.windows(3);
    assert_eq!(iter.next().unwrap(), &['l', 'o', 'r']);
    assert_eq!(iter.next().unwrap(), &['o', 'r', 'e']);
    assert_eq!(iter.next().unwrap(), &['r', 'e', 'm']);
    assert!(iter.next().is_none());
}
