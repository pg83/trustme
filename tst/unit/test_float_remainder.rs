// Rust's `%` on floats truncates the quotient and keeps the dividend's sign,
// which is `fmod`. The emitter used `remainder`, which rounds the quotient to
// nearest, so `7.0 % 4.0` came out as -1.0 rather than 3.0.
//
// Same shape as the library doctests for `f32::div_euclid` and
// `f64::div_euclid`.
fn main() {
    assert_eq!(7.0f32 % 4.0, 3.0);
    assert_eq!(7.0f64 % 4.0, 3.0);

    // The sign follows the dividend, not the divisor.
    assert_eq!((-7.0f32) % 4.0, -3.0);
    assert_eq!(7.0f32 % -4.0, 3.0);
    assert_eq!((-7.0f64) % 4.0, -3.0);
    assert_eq!(7.0f64 % -4.0, 3.0);

    // Below half the divisor, where truncating and rounding agree.
    assert_eq!(5.0f32 % 4.0, 1.0);
    assert_eq!(5.0f64 % 4.0, 1.0);

    // The library functions built on it.
    let a: f32 = 7.0;
    assert_eq!(a.div_euclid(4.0), 1.0);
    assert_eq!((-a).div_euclid(4.0), -2.0);
    assert_eq!(a.div_euclid(-4.0), -1.0);
    assert_eq!((-a).div_euclid(-4.0), 2.0);
    assert_eq!(a.rem_euclid(4.0), 3.0);
    assert_eq!((-a).rem_euclid(4.0), 1.0);

    let b: f64 = 7.0;
    assert_eq!(b.div_euclid(4.0), 1.0);
    assert_eq!((-b).div_euclid(4.0), -2.0);
    assert_eq!(b.rem_euclid(4.0), 3.0);
    assert_eq!((-b).rem_euclid(4.0), 1.0);

    // A remainder that is not a whole number.
    assert!((7.5f64 % 2.0 - 1.5).abs() < 1e-12);
}
