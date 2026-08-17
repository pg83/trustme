// Casting between the 128-bit integers and floats went wrong in both
// directions. A negative 128-bit value was handed to the conversion as its raw
// two's-complement bits, so its magnitude came out as an enormous positive
// number; and a float cast to a 128-bit integer was truncated through the low
// half, ignoring everything above 64 bits.
//
// Same shape as the upstream tests numbers-arithmetic/i128.rs and u128.rs.
fn main() {
    // Small values, both signs.
    let z: i128 = 0xABCDEF;
    assert_eq!(z as f64, 11259375.0);
    assert_eq!(z as f32, 11259375.0f32);
    assert_eq!((-z) as f64, -11259375.0);
    assert_eq!((-z) as f32, -11259375.0f32);
    assert_eq!(((-z) as f64) as i128, -z);
    assert_eq!(((-z) as f32) as i128, -z);
    assert_eq!(((-z) as f64 * 16.0) as i128, -z * 16);

    // Values that need both halves.
    let l: u128 = 432 << 100;
    assert_eq!((l as f32) as u128, l);
    assert_eq!((l as f64) as u128, l);
    let m: i128 = -(432i128 << 100);
    assert_eq!((m as f64) as i128, m);
    assert_eq!((m as f32) as i128, m);

    // A float cast to an integer saturates, and NaN becomes zero.
    assert_eq!((-1.5f64) as u128, 0);
    assert_eq!(f64::NAN as u128, 0);
    assert_eq!(f64::NAN as i128, 0);
    assert_eq!(1e40f64 as u128, u128::MAX);
    assert_eq!(1e40f64 as i128, i128::MAX);
    assert_eq!((-1e40f64) as i128, i128::MIN);

    // The extremes round-trip through f64 as closely as f64 allows.
    assert_eq!(i128::MIN as f64, -170141183460469231731687303715884105728.0);
}
