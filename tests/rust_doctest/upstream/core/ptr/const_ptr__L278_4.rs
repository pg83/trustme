// Extracted from library/core/src/ptr/const_ptr.rs:278
#![allow(unused)]
fn main() {
    let ptr: *const u8 = &10u8 as *const u8;

    unsafe {
        if let Some(val_back) = ptr.as_ref() {
            assert_eq!(val_back, &10);
        }
    }
}
