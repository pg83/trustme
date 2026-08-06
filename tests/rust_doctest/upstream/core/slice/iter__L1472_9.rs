// Extracted from library/core/src/slice/iter.rs:1472
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let mut iter = slice.chunks(2);
    assert_eq!(iter.next(), Some(&['l', 'o'][..]));
    assert_eq!(iter.next(), Some(&['r', 'e'][..]));
    assert_eq!(iter.next(), Some(&['m'][..]));
    assert_eq!(iter.next(), None);
}
