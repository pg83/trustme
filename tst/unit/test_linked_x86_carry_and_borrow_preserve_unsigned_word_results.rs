#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

extern "unadjusted" {
    #[link_name = "llvm.x86.addcarry.32"]
    fn add(carry: u8, left: u32, right: u32) -> (u8, u32);
    #[link_name = "llvm.x86.subborrow.32"]
    fn subtract(borrow: u8, left: u32, right: u32) -> (u8, u32);
}
fn main() {
    unsafe {
        assert_eq!(add(0, u32::MAX, 1), (1, 0));
        assert_eq!(add(1, u32::MAX, 0), (1, 0));
        assert_eq!(add(1, 13, 21), (0, 35));
        assert_eq!(subtract(1, 0, 0), (1, u32::MAX));
        assert_eq!(subtract(0, 37, 13), (0, 24));
        assert_eq!(subtract(1, 37, 13), (0, 23));
    }
}
