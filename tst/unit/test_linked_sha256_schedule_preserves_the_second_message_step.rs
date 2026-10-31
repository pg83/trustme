#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)] #[derive(Clone, Copy)] struct Words([u32; 4]);
extern "unadjusted" {
    #[link_name = "llvm.x86.sha256msg2"] fn schedule(current: Words, prior: Words) -> Words;
}
#[target_feature(enable = "sha")]
unsafe fn check() {
    let result: [u32; 4] = core::mem::transmute(schedule(Words([1,2,3,4]), Words([0,0,5,6])));
    assert_eq!(result, [139265, 245762, 1409327244, 2550218997]);
}
fn main() { if is_x86_feature_detected!("sha") { unsafe { check(); } } }
