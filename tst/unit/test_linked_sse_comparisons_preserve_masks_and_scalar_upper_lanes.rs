#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

#[repr(simd)] #[derive(Clone, Copy)] struct Floats([f32; 4]);
#[repr(simd)] #[derive(Clone, Copy)] struct Doubles([f64; 2]);
extern "unadjusted" {
    #[link_name = "llvm.x86.sse.cmp.ps"] fn packed(left: Floats, right: Floats, predicate: i8) -> Floats;
    #[link_name = "llvm.x86.sse.cmp.ss"] fn scalar(left: Floats, right: Floats, predicate: i8) -> Floats;
    #[link_name = "llvm.x86.sse2.cmp.sd"] fn double(left: Doubles, right: Doubles, predicate: i8) -> Doubles;
}
fn main() {
    unsafe {
        let left = Floats([1.0, f32::NAN, 3.0, -0.0]);
        let right = Floats([1.0, 2.0, 2.0, 0.0]);
        let equal: [u32; 4] = core::mem::transmute(packed(left, right, 0));
        assert_eq!(equal, [u32::MAX, 0, 0, u32::MAX]);
        let unordered: [u32; 4] = core::mem::transmute(packed(left, right, 3));
        assert_eq!(unordered, [0, u32::MAX, 0, 0]);
        let single: [u32; 4] = core::mem::transmute(scalar(Floats([5.0,41.0,43.0,47.0]), Floats([7.0,53.0,59.0,61.0]), 1));
        assert_eq!(single, [u32::MAX, 41_f32.to_bits(), 43_f32.to_bits(), 47_f32.to_bits()]);
        let wide: [u64; 2] = core::mem::transmute(double(Doubles([5.0,41.0]), Doubles([7.0,53.0]), 1));
        assert_eq!(wide, [u64::MAX, 41_f64.to_bits()]);
    }
}
