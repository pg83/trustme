// Extracted from library/core/src/slice/mod.rs:1846
#![allow(unused)]
fn main() {
    let slice = &[1, 1, 2, 3, 2, 3, 2, 3, 4];
    
    let mut iter = slice.chunk_by(|a, b| a <= b);
    
    assert_eq!(iter.next(), Some(&[1, 1, 2, 3][..]));
    assert_eq!(iter.next(), Some(&[2, 3][..]));
    assert_eq!(iter.next(), Some(&[2, 3, 4][..]));
    assert_eq!(iter.next(), None);
}
