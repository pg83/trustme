#![feature(core_intrinsics, f16, f128)]
trait Values {
    const A: f16;
    const B: f32;
    const C: f64;
    const D: f128;
}
struct Infinite;
impl Values for Infinite {
    const A: f16 = f16::NEG_INFINITY;
    const B: f32 = f32::NEG_INFINITY;
    const C: f64 = f64::NEG_INFINITY;
    const D: f128 = f128::NEG_INFINITY;
}
const fn absolute<S: Values>() -> (u16, u32, u64, u128) {
    unsafe { (
        S::A.abs().to_bits(),
        S::B.abs().to_bits(),
        S::C.abs().to_bits(),
        S::D.abs().to_bits(),
    ) }
}
const fn negative<S: Values>() -> (u16, u32, u64, u128) {
    unsafe { (
        core::intrinsics::copysignf16(S::A, -0.0).to_bits(),
        core::intrinsics::copysignf32(S::B, -0.0).to_bits(),
        core::intrinsics::copysignf64(S::C, -0.0).to_bits(),
        core::intrinsics::copysignf128(S::D, -0.0).to_bits(),
    ) }
}
const ABS: (u16, u32, u64, u128) = absolute::<Infinite>();
const NEGATIVE: (u16, u32, u64, u128) = negative::<Infinite>();
fn main() {
    assert_eq!(ABS, (0x7c00, 0x7f80_0000, 0x7ff0_0000_0000_0000, 0x7fffu128 << 112));
    assert_eq!(NEGATIVE, (0xfc00, 0xff80_0000, 0xfff0_0000_0000_0000, 0xffffu128 << 112));
}
