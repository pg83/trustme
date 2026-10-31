#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

extern "unadjusted" {
    #[link_name = "llvm.x86.xgetbv"] fn read(index: u32) -> u64;
}
#[target_feature(enable = "xsave")]
unsafe fn enabled() -> u64 { read(0) }
fn main() {
    if is_x86_feature_detected!("xsave") { assert_eq!(unsafe { enabled() } & 1, 1); }
}
