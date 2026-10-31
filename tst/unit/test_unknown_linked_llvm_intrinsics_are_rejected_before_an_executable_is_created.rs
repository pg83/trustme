//@ compile-fail: Unsupported LLVM intrinsic
#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

extern "unadjusted" {
    #[link_name = "llvm.trustme.unsupported.v1"] fn unavailable(value: u32) -> u32;
}
fn main() { assert_eq!(unsafe { unavailable(13) }, 13); }
