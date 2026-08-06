// Extracted from library/core/src/mem/maybe_uninit.rs:1210
#![allow(unused)]
#![feature(maybe_uninit_fill)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut buf = [const { MaybeUninit::<usize>::uninit() }; 5];
    let initialized = buf.write_with(|idx| idx + 1);
    assert_eq!(initialized, &mut [1, 2, 3, 4, 5]);
}
