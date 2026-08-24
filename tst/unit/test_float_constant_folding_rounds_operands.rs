//@ compile-flags: -O

fn main() {
    let value_f32: f32 = 0.57110405f32 - 0.8529074f32;
    assert_eq!(value_f32.to_bits(), 0xbe90_4888);

    let value_f64: f64 = 0.2899142286263785f64 - 0.5247429107024408f64;
    assert_eq!(value_f64.to_bits(), 0xbfce_0edd_c2d6_f8d8);
}
