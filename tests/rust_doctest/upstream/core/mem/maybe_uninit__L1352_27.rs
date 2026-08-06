// Extracted from library/core/src/mem/maybe_uninit.rs:1352
#![allow(unused)]
#![feature(maybe_uninit_as_bytes, maybe_uninit_write_slice, maybe_uninit_slice)]
fn main() {
    use std::mem::MaybeUninit;
    
    let mut uninit = [MaybeUninit::<u16>::uninit(), MaybeUninit::<u16>::uninit()];
    let uninit_bytes = uninit.as_bytes_mut();
    uninit_bytes.write_copy_of_slice(&[0x12, 0x34, 0x56, 0x78]);
    let vals = unsafe { uninit.assume_init_ref() };
    if cfg!(target_endian = "little") {
        assert_eq!(vals, &[0x3412u16, 0x7856u16]);
    } else {
        assert_eq!(vals, &[0x1234u16, 0x5678u16]);
    }
}
