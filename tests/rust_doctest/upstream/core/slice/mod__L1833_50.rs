// Extracted from library/core/src/slice/mod.rs:1833
#![allow(unused)]
fn main() {
    let slice = &[1, 1, 1, 3, 3, 2, 2, 2];

    let mut iter = slice.chunk_by(|a, b| a == b);

    assert_eq!(iter.next(), Some(&[1, 1, 1][..]));
    assert_eq!(iter.next(), Some(&[3, 3][..]));
    assert_eq!(iter.next(), Some(&[2, 2, 2][..]));
    assert_eq!(iter.next(), None);
}
