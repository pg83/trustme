#![feature(layout_for_ptr)]

use std::{mem, ptr};

#[repr(C, packed)]
struct PackedSlice {
    tail: [u32],
}

fn main() {
    let storage = [0u8; 4];
    let value: *const PackedSlice = ptr::from_raw_parts(storage.as_ptr(), 1);
    assert_eq!(unsafe { mem::align_of_val_raw(value) }, 1);
}
