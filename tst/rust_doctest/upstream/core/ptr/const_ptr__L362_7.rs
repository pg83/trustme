// Extracted from library/core/src/ptr/const_ptr.rs:362
#![allow(unused)]
#![feature(ptr_as_uninit)]
fn main() {

    let ptr: *const u8 = &10u8 as *const u8;

    unsafe {
        if let Some(val_back) = ptr.as_uninit_ref() {
            assert_eq!(val_back.assume_init(), 10);
        }
    }
}
