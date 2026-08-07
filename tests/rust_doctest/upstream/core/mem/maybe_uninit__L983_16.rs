// Extracted from library/core/src/mem/maybe_uninit.rs:983
#![allow(unused)]
#![feature(maybe_uninit_as_bytes)]
fn main() {
    use std::mem::MaybeUninit;

    let val = 0x12345678_i32;
    let mut uninit = MaybeUninit::new(val);
    let uninit_bytes = uninit.as_bytes_mut();
    if cfg!(target_endian = "little") {
        uninit_bytes[0].write(0xcd);
    } else {
        uninit_bytes[3].write(0xcd);
    }
    let val2 = unsafe { uninit.assume_init() };
    assert_eq!(val2, 0x123456cd);
}
