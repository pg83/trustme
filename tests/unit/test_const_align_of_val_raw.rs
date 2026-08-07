#![feature(layout_for_ptr)]

use std::{mem, ptr};

#[repr(C, packed)]
struct PackedSlice {
    tail: [u32],
}

const PACKED_ALIGN: usize = {
    let storage = [0u8; 4];
    let value: *const PackedSlice = ptr::from_raw_parts(storage.as_ptr(), 1);
    unsafe { mem::align_of_val_raw(value) }
};

const I16_ALIGN: usize = unsafe { mem::align_of_val_raw(0x100 as *const i16) };

fn main() {
    assert_eq!(PACKED_ALIGN, 1);
    assert_eq!(I16_ALIGN, mem::align_of::<i16>());
}
