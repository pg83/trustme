// Extracted from library/core/src/slice/iter.rs:2315
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let mut iter = slice.rchunks(2);
    assert_eq!(iter.next(), Some(&['e', 'm'][..]));
    assert_eq!(iter.next(), Some(&['o', 'r'][..]));
    assert_eq!(iter.next(), Some(&['l'][..]));
    assert_eq!(iter.next(), None);
}
