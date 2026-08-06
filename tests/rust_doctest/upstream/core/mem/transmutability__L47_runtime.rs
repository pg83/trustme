// Extracted from library/core/src/mem/transmutability.rs:47
#![allow(unused)]
#![feature(transmutability)]
fn main() {
    
    use core::mem::{Assume, TransmuteFrom};
    
    let src = 42u8; // size = 1
    
    #[repr(C, align(2))]
    struct Dst(u8); // size = 2
    
    let _ = unsafe {
        <Dst as TransmuteFrom<u8, { Assume::SAFETY }>>::transmute(src)
    };
}
