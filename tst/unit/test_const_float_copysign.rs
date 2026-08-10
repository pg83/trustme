#![feature(f16, f128)]

const COPYSIGN_F16_BITS: u16 = f16::from_bits(0x7e55).copysign(-0.0).to_bits();
const COPYSIGN_F32_BITS: u32 = f32::from_bits(0x7fc0_1234).copysign(-0.0).to_bits();
const COPYSIGN_F64_BITS: u64 = f64::from_bits(0x7ff8_0000_0000_1234).copysign(-0.0).to_bits();
const COPYSIGN_F128_BITS: u128 = f128::from_bits(0x7fff_8000_0000_0000_0000_0000_0000_1234).copysign(-0.0).to_bits();

#[inline(never)]
fn runtime_copysign_f32(value: f32, sign: f32) -> f32 {
    value.copysign(sign)
}

#[inline(never)]
fn runtime_copysign_f64(value: f64, sign: f64) -> f64 {
    value.copysign(sign)
}

fn main() {
    assert_eq!(COPYSIGN_F16_BITS, 0xfe55);
    assert_eq!(COPYSIGN_F32_BITS, 0xffc0_1234);
    assert_eq!(COPYSIGN_F64_BITS, 0xfff8_0000_0000_1234);
    assert_eq!(COPYSIGN_F128_BITS, 0xffff_8000_0000_0000_0000_0000_0000_1234);

    assert_eq!(runtime_copysign_f32(1.0, -2.0).to_bits(), (-1.0f32).to_bits());
    assert_eq!(runtime_copysign_f64(-1.0, 2.0).to_bits(), 1.0f64.to_bits());
}
