// Extracted from library/core/src/mem/mod.rs:548
#![allow(unused)]
#![feature(layout_for_ptr)]
fn main() {
    use std::mem;

    assert_eq!(4, unsafe { mem::align_of_val_raw(&5i32) });
}
