// Extracted from library/core/src/slice/mod.rs:1888
#![allow(unused)]
fn main() {
    let slice = &mut [1, 1, 2, 3, 2, 3, 2, 3, 4];

    let mut iter = slice.chunk_by_mut(|a, b| a <= b);

    assert_eq!(iter.next(), Some(&mut [1, 1, 2, 3][..]));
    assert_eq!(iter.next(), Some(&mut [2, 3][..]));
    assert_eq!(iter.next(), Some(&mut [2, 3, 4][..]));
    assert_eq!(iter.next(), None);
}
