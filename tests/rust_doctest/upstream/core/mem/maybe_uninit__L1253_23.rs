// Extracted from library/core/src/mem/maybe_uninit.rs:1253
#![allow(unused)]
#![feature(maybe_uninit_fill)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut buf = [const { MaybeUninit::uninit() }; 5];
    
    let iter = [1, 2, 3].into_iter().cycle();
    let (initialized, remainder) = buf.write_iter(iter);
    
    assert_eq!(initialized, &mut [1, 2, 3, 1, 2]);
    assert_eq!(remainder.len(), 0);
}
