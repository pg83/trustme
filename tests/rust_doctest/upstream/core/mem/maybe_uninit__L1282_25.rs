// Extracted from library/core/src/mem/maybe_uninit.rs:1282
#![allow(unused)]
#![feature(maybe_uninit_fill)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut buf = [const { MaybeUninit::uninit() }; 3];
    let mut iter = [1, 2, 3, 4, 5].into_iter();
    let (initialized, remainder) = buf.write_iter(iter.by_ref());
    
    assert_eq!(initialized, &mut [1, 2, 3]);
    assert_eq!(remainder.len(), 0);
    assert_eq!(iter.as_slice(), &[4, 5]);
}
