// Extracted from library/core/src/ptr/non_null.rs:1555
#![allow(unused)]
#![feature(allocator_api, ptr_as_uninit)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::{Allocator, Layout, Global};
        use std::mem::MaybeUninit;
        use std::ptr::NonNull;
        
        let memory: NonNull<[u8]> = Global.allocate(Layout::new::<[u8; 32]>())?;
        // This is safe as `memory` is valid for reads and writes for `memory.len()` many bytes.
        // Note that calling `memory.as_mut()` is not allowed here as the content may be uninitialized.
        #[allow(unused_variables)]
        let slice: &mut [MaybeUninit<u8>] = unsafe { memory.as_uninit_slice_mut() };
        // Prevent leaks for Miri.
        unsafe { Global.deallocate(memory.cast(), Layout::new::<[u8; 32]>()); }
        Ok::<_, std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
