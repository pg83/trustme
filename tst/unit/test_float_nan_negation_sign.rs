const NEG_F32_NAN: f32 = -f32::NAN;
const NEG_F64_NAN: f64 = -f64::NAN;

fn main() {
    assert_eq!(NEG_F32_NAN.to_bits(), f32::NAN.to_bits() | (1 << 31));
    assert_eq!(NEG_F64_NAN.to_bits(), f64::NAN.to_bits() | (1 << 63));
}
