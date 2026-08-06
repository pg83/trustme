// Extracted from library/core/src/primitive_docs.rs:590
#![allow(unused)]
fn main() {
    mod libc {
    pub unsafe fn malloc(_size: usize) -> *mut core::ffi::c_void { core::ptr::NonNull::dangling().as_ptr() }
    pub unsafe fn free(_ptr: *mut core::ffi::c_void) {}
    }
    #[cfg(any())]
    #[allow(unused_extern_crates)]
    extern crate libc;
    
    unsafe {
        let my_num: *mut i32 = libc::malloc(size_of::<i32>()) as *mut i32;
        if my_num.is_null() {
            panic!("failed to allocate memory");
        }
        libc::free(my_num as *mut core::ffi::c_void);
    }
}
