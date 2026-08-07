#![feature(layout_for_ptr)]

use std::{mem, ptr};

const WORD_SIZE: usize = mem::size_of_val(&0u64);
const VALUES: &[u16] = &[10, 20, 30];
const SLICE_SIZE: usize = mem::size_of_val(VALUES);
const HUGE_SLICE_SIZE: usize = unsafe {
    mem::size_of_val_raw(ptr::slice_from_raw_parts(ptr::null::<u8>(), isize::MAX as usize))
};

fn main() {
    assert_eq!(WORD_SIZE, mem::size_of::<u64>());
    assert_eq!(SLICE_SIZE, 3 * mem::size_of::<u16>());
    assert_eq!(HUGE_SLICE_SIZE, isize::MAX as usize);
}
