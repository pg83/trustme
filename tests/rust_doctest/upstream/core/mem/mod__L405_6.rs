// Extracted from library/core/src/mem/mod.rs:405
#![allow(unused)]
#![feature(layout_for_ptr)]
fn main() {
    use std::mem;
    
    assert_eq!(4, size_of_val(&5i32));
    
    let x: [u8; 13] = [0; 13];
    let y: &[u8] = &x;
    assert_eq!(13, unsafe { mem::size_of_val_raw(y) });
}
