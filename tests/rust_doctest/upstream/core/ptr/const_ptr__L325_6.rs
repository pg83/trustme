// Extracted from library/core/src/ptr/const_ptr.rs:325
#![allow(unused)]
#![feature(ptr_as_ref_unchecked)]
fn main() {
    let ptr: *const u8 = &10u8 as *const u8;
    
    unsafe {
        assert_eq!(ptr.as_ref_unchecked(), &10);
    }
}
