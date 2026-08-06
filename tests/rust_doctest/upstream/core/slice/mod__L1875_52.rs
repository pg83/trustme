// Extracted from library/core/src/slice/mod.rs:1875
#![allow(unused)]
fn main() {
    let slice = &mut [1, 1, 1, 3, 3, 2, 2, 2];
    
    let mut iter = slice.chunk_by_mut(|a, b| a == b);
    
    assert_eq!(iter.next(), Some(&mut [1, 1, 1][..]));
    assert_eq!(iter.next(), Some(&mut [3, 3][..]));
    assert_eq!(iter.next(), Some(&mut [2, 2, 2][..]));
    assert_eq!(iter.next(), None);
}
