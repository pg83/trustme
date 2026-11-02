//@ compile-variant: -O

const BITS: (u32, u32, u64, u64) = (
    (-0.0f32).to_bits(),
    (-f32::INFINITY).to_bits(),
    (-0.0f64).to_bits(),
    (-f64::NEG_INFINITY).to_bits(),
);

fn main() {
    assert_eq!(BITS, (0x8000_0000, 0xff80_0000, 0x8000_0000_0000_0000, 0x7ff0_0000_0000_0000));
}
