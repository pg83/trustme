#![feature(core_intrinsics, f16, f128)]

const ABS16: f16 = unsafe { core::intrinsics::fabsf16(-1.5) };
const ABS128: f128 = unsafe { core::intrinsics::fabsf128(-2.0) };
const ABS32: f32 = unsafe { core::intrinsics::fabsf32(-3.5) };
const ABS64: f64 = unsafe { core::intrinsics::fabsf64(-7.25) };
const SIGN32: f32 = unsafe { core::intrinsics::copysignf32(2.5, -0.0) };
const SIGN64: f64 = unsafe { core::intrinsics::copysignf64(-9.5, 0.0) };
const ZERO32: f32 = unsafe { core::intrinsics::copysignf32(0.0, -1.0) };
const ZERO128: f128 = unsafe { core::intrinsics::copysignf128(0.0, -1.0) };

fn main() {
    assert_eq!(ABS16.to_bits(), 0x3e00);
    assert_eq!(ABS128.to_bits(), 0x4000u128 << 112);
    assert_eq!(ABS32, 3.5);
    assert_eq!(ABS64, 7.25);
    assert_eq!(SIGN32, -2.5);
    assert_eq!(SIGN64, 9.5);
    assert_eq!(ZERO32.to_bits(), 1 << 31);
    assert_eq!(ZERO128.to_bits(), 1 << 127);
}
