//@ run-pass
// num-rational's `test_big_ratio_to_f64` expects
// `411522630329218100000000000000000000000000000f64`. An integer literal
// with a float suffix is a float literal read from its digits (rustc's
// `LitKind::from_token_lit` hands it to `filtered_float_lit`); the digits
// were accumulated into a u128, which wrapped past 2^128.
fn main() {
    let big = 411522630329218100000000000000000000000000000f64;
    assert_eq!(big, 4.115226303292181e44);
    assert_eq!(340282366920938463463374607431768211456f64, 2f64.powi(128));
    assert_eq!(7f64, 7.0);
    assert_eq!(1_0f32, 10.0);
}
