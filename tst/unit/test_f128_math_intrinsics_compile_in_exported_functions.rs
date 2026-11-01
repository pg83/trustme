//@ crate-type: lib
#![feature(core_intrinsics, f128)]

pub fn rounding(value: f128) -> [f128; 4] {
    unsafe { [core::intrinsics::ceilf128(value), core::intrinsics::floorf128(value),
        core::intrinsics::truncf128(value), core::intrinsics::round_ties_even_f128(value)] }
}

pub fn powers(value: f128) -> [f128; 4] {
    unsafe { [core::intrinsics::powif128(value, -3), core::intrinsics::powf128(value, 3.0),
        core::intrinsics::expf128(value), core::intrinsics::exp2f128(value)] }
}

pub fn logarithms(value: f128) -> [f128; 4] {
    unsafe { [core::intrinsics::logf128(value), core::intrinsics::log10f128(value),
        core::intrinsics::log2f128(value), core::intrinsics::sqrtf128(value)] }
}

pub fn trigonometry(value: f128) -> [f128; 2] {
    unsafe { [core::intrinsics::cosf128(value), core::intrinsics::sinf128(value)] }
}

pub fn fused(left: f128, right: f128, addend: f128) -> f128 {
    unsafe { core::intrinsics::fmaf128(left, right, addend) }
}

pub unsafe fn finite_remainder(left: f128, right: f128) -> f128 {
    core::intrinsics::frem_fast(left, right)
}
