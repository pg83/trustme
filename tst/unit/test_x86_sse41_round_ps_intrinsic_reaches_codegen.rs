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

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse41.round.ps"]
    fn round_ps(input: F32x4, mode: i32) -> F32x4;
}

const ROUND_NEAREST: i32 = 0x00;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_nearest(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_NEAREST) }
}

const ROUND_FLOOR: i32 = 0x01;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_floor(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_FLOOR) }
}

const ROUND_CEIL: i32 = 0x02;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_ceil(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CEIL) }
}

const ROUND_TRUNCATE: i32 = 0x03;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_truncate(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_TRUNCATE) }
}

const ROUND_CURRENT: i32 = 0x04;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_current(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CURRENT) }
}

const ROUND_CURRENT_IGNORES_LOW_BITS: i32 = 0x07;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_current_ignores_low_bits(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CURRENT_IGNORES_LOW_BITS) }
}

const ROUND_NEAREST_SUPPRESS_PRECISION: i32 = 0x08;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_nearest_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_NEAREST_SUPPRESS_PRECISION) }
}

const ROUND_FLOOR_SUPPRESS_PRECISION: i32 = 0x09;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_floor_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_FLOOR_SUPPRESS_PRECISION) }
}

const ROUND_CEIL_SUPPRESS_PRECISION: i32 = 0x0a;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_ceil_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CEIL_SUPPRESS_PRECISION) }
}

const ROUND_TRUNCATE_SUPPRESS_PRECISION: i32 = 0x0b;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_truncate_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_TRUNCATE_SUPPRESS_PRECISION) }
}

const ROUND_CURRENT_SUPPRESS_PRECISION: i32 = 0x0c;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_current_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CURRENT_SUPPRESS_PRECISION) }
}

const ROUND_CURRENT_IGNORES_LOW_BITS_SUPPRESS_PRECISION: i32 = 0x0f;

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_ps_current_ignores_low_bits_suppress_precision(input: F32x4) -> F32x4 {
    unsafe { round_ps(input, ROUND_CURRENT_IGNORES_LOW_BITS_SUPPRESS_PRECISION) }
}
