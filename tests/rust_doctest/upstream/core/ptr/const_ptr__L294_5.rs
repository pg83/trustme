// Extracted from library/core/src/ptr/const_ptr.rs:294
#![allow(unused)]
fn main() {
    let ptr: *const u8 = &10u8 as *const u8;
    
    unsafe {
        let val_back = &*ptr;
        assert_eq!(val_back, &10);
    }
}
