const LOCAL_NAN_BITS: u64 = (0.0f64 / 0.0).to_bits();
const NAN_BITS: u64 = f64::NAN.to_bits();
const NAN_SIGN_MASKED: u64 = NAN_BITS & (1u64 << 63);
const NAN_SIGN_POSITIVE: bool = f64::NAN.is_sign_positive();

fn main() {
    assert_eq!(LOCAL_NAN_BITS, 0x7ff8_0000_0000_0000, "local NaN");
    assert_eq!(NAN_BITS, 0x7ff8_0000_0000_0000, "libstd NaN");
    assert_eq!(NAN_SIGN_MASKED, 0);
    assert!(NAN_SIGN_POSITIVE);
}
