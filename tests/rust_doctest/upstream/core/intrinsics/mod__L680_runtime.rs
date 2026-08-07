// Extracted from library/core/src/intrinsics/mod.rs:680
#![allow(unused)]
fn main() {
    let ptr: *mut i32 = &mut 0;
    let ref_transmuted = unsafe {
        std::mem::transmute::<*mut i32, &mut i32>(ptr)
    };

    // Use a reborrow instead
    let ref_casted = unsafe { &mut *ptr };
}
