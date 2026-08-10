// Extracted from library/core/src/mem/maybe_uninit.rs:1325
#![allow(unused)]
#![feature(maybe_uninit_as_bytes, maybe_uninit_write_slice, maybe_uninit_slice)]
fn main() {
    use std::mem::MaybeUninit;

    let uninit = [MaybeUninit::new(0x1234u16), MaybeUninit::new(0x5678u16)];
    let uninit_bytes = uninit.as_bytes();
    let bytes = unsafe { uninit_bytes.assume_init_ref() };
    let val1 = u16::from_ne_bytes(bytes[0..2].try_into().unwrap());
    let val2 = u16::from_ne_bytes(bytes[2..4].try_into().unwrap());
    assert_eq!(&[val1, val2], &[0x1234u16, 0x5678u16]);
}
