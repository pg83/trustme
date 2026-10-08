// stdarch declares LLVM's intrinsics in `unsafe extern "unadjusted"` blocks,
// `llvm.x86.vcvtps2ph.256` (behind `_mm256_cvtps_ph`) among them. rustc
// passes every argument of the "unadjusted" ABI directly
// (`fn_abi_adjust_for_abi` in rustc_ty_utils/src/abi.rs), even an aggregate
// wider than two pointers such as this `f32x8`; only the Rust ABI passes
// those by reference.
#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]

#[repr(simd)]
#[derive(Copy, Clone)]
struct F32x8([f32; 8]);

#[repr(simd)]
#[derive(Copy, Clone)]
struct I16x8([i16; 8]);

#[allow(improper_ctypes)]
unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.vcvtps2ph.256"]
    fn vcvtps2ph256(a: F32x8, rounding: i32) -> I16x8;
}

#[target_feature(enable = "avx,f16c")]
unsafe fn halves(x: f32) -> [u16; 8] {
    unsafe { std::mem::transmute::<I16x8, [u16; 8]>(vcvtps2ph256(F32x8([x; 8]), 0)) }
}

fn main() {
    if is_x86_feature_detected!("avx") && is_x86_feature_detected!("f16c") {
        assert_eq!(unsafe { halves(1.0) }, [0x3c00; 8]);
        assert_eq!(unsafe { halves(-2.0) }, [0xc000; 8]);
    }
}
