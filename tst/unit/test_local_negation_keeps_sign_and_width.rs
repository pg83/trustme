fn integers() -> [i128; 3] {
    let small = -117i8;
    let wide = 1i128 << 100;
    let mask = -1i128;
    [(-small) as i128, -wide, !mask]
}
fn floats() -> [f64; 4] {
    let positive = 3.5f64;
    let negative = -2.25f64;
    let zero = 0.0f64;
    let minus_zero = -0.0f64;
    [-positive, -negative, -zero, -minus_zero]
}
fn main() {
    assert_eq!(integers(), [117, -(1i128 << 100), 0]);
    let values = floats();
    assert_eq!(&values[..2], &[-3.5, 2.25]);
    assert_eq!(values[2].to_bits(), (-0.0f64).to_bits());
    assert_eq!(values[3].to_bits(), 0.0f64.to_bits());
}
