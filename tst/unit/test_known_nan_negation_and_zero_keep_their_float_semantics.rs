//@ compile-variant: -O
#[inline(never)]
fn inspect() {
    let single = f32::NAN;
    let double = f64::NAN;
    let single_negative = -single;
    let double_negative = -double;
    assert!(single_negative.is_nan());
    assert!(double_negative.is_nan());
    let zero = 0.0f64;
    assert_eq!((-zero).to_bits(), 1u64 << 63);
}
fn main() { inspect(); }
