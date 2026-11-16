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
pub struct F32x4([f32; 4]);

const ROUND_NEAREST: i32 = 0;
const ROUND_DOWN: i32 = 1;
const ROUND_UP: i32 = 2;
const ROUND_ZERO: i32 = 3;
const ROUND_CURRENT: i32 = 4;
const ROUND_NEAREST_NO_EXC: i32 = 8;
const ROUND_DOWN_NO_EXC: i32 = 9;
const ROUND_UP_NO_EXC: i32 = 10;
const ROUND_ZERO_NO_EXC: i32 = 11;
const ROUND_CURRENT_NO_EXC: i32 = 12;

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse41.round.ss"]
    fn round_ss(upper: F32x4, source: F32x4, mode: i32) -> F32x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_nearest(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_NEAREST) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_down(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_DOWN) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_up(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_UP) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_zero(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_ZERO) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_current(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_CURRENT) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_nearest_no_exc(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_NEAREST_NO_EXC) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_down_no_exc(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_DOWN_NO_EXC) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_up_no_exc(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_UP_NO_EXC) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_zero_no_exc(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_ZERO_NO_EXC) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_low_f32_current_no_exc(upper: F32x4, source: F32x4) -> F32x4 {
    unsafe { round_ss(upper, source, ROUND_CURRENT_NO_EXC) }
}
