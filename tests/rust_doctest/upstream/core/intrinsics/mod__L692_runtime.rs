// Extracted from library/core/src/intrinsics/mod.rs:692
#![allow(unused)]
fn main() {
    let ptr = &mut 0;
    let val_transmuted = unsafe {
        std::mem::transmute::<&mut i32, &mut u32>(ptr)
    };

    // Now, put together `as` and reborrowing - note the chaining of `as`
    // `as` is not transitive
    let val_casts = unsafe { &mut *(ptr as *mut i32 as *mut u32) };
}
