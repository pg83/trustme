//@ crate-type: lib
//@ compile-only
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![no_core]
#![allow(internal_features, improper_ctypes, improper_ctypes_definitions)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}

#[repr(simd)]
pub struct I64x4([i64; 4]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx2.vperm2i128"]
    fn vperm2i128(lhs: I64x4, rhs: I64x4, control: i8) -> I64x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn lhs_high_rhs_low(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x21) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn rhs_high_lhs_low(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x03) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn rhs_low_lhs_high(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x12) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn lhs_low_rhs_high(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x30) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn rhs_low_zero_high(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x82u8 as i8) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn zero_low_lhs_high(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x1b) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn zero_both(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x88u8 as i8) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn ignored_control_bits(lhs: I64x4, rhs: I64x4) -> I64x4 {
    unsafe { vperm2i128(lhs, rhs, 0x65) }
}
