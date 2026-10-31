#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

extern "unadjusted" {
    #[link_name = "llvm.x86.addcarryx.u32"]
    fn add(carry: u8, left: u32, right: u32, output: *mut u32) -> u8;
}
#[target_feature(enable = "adx")]
unsafe fn check() {
    let mut output = 123;
    assert_eq!(add(1, u32::MAX, 0, &mut output), 1);
    assert_eq!(output, 0);
    assert_eq!(add(0, 13, 21, &mut output), 0);
    assert_eq!(output, 34);
}
fn main() { if is_x86_feature_detected!("adx") { unsafe { check(); } } }
