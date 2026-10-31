#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.lfence"] fn load_fence();
    #[link_name = "llvm.x86.sse2.mfence"] fn full_fence();
    #[link_name = "llvm.x86.sse2.pause"] fn pause();
}
fn main() {
    let mut value = 13_u32;
    unsafe {
        core::ptr::write_volatile(&mut value, 37);
        full_fence();
        pause();
        load_fence();
        assert_eq!(core::ptr::read_volatile(&value), 37);
    }
}
