#![feature(core_intrinsics)]

fn main() {
    let zero32 = std::hint::black_box(0.0f32);
    let one32 = std::hint::black_box(1.0f32);
    let nine32 = std::hint::black_box(9.0f32);
    let value32 = std::hint::black_box(2.75f32);
    unsafe {
        assert_eq!(core::intrinsics::expf32(zero32), 1.0);
        assert_eq!(core::intrinsics::exp2f32(3.0 * one32), 8.0);
        assert_eq!(core::intrinsics::logf32(one32), 0.0);
        assert_eq!(core::intrinsics::log10f32(one32), 0.0);
        assert_eq!(core::intrinsics::log2f32(one32), 0.0);
        assert_eq!(core::intrinsics::sqrtf32(nine32), 3.0);
        assert_eq!(core::intrinsics::ceilf32(value32), 3.0);
        assert_eq!(core::intrinsics::floorf32(value32), 2.0);
        assert_eq!(core::intrinsics::sinf32(zero32), 0.0);
        assert_eq!(core::intrinsics::cosf32(zero32), 1.0);
    }
    let zero64 = std::hint::black_box(0.0f64);
    let one64 = std::hint::black_box(1.0f64);
    unsafe {
        assert_eq!(core::intrinsics::expf64(zero64), 1.0);
        assert_eq!(core::intrinsics::exp2f64(3.0 * one64), 8.0);
        assert_eq!(core::intrinsics::logf64(one64), 0.0);
        assert_eq!(core::intrinsics::log10f64(one64), 0.0);
        assert_eq!(core::intrinsics::log2f64(one64), 0.0);
        assert_eq!(core::intrinsics::sqrtf64(9.0 * one64), 3.0);
        assert_eq!(core::intrinsics::ceilf64(2.75 * one64), 3.0);
        assert_eq!(core::intrinsics::floorf64(2.75 * one64), 2.0);
        assert_eq!(core::intrinsics::sinf64(zero64), 0.0);
        assert_eq!(core::intrinsics::cosf64(zero64), 1.0);
    }
}
