#![feature(f128)]
#[inline(never)]
fn greater(a: f128, b: f128) -> bool { a > b }
fn main() {
    for (a, b, expected) in [(7.5f128, 2.5f128, true), (2.5, 7.5, false), (-0.0, 0.0, false), (0.0, -0.0, false)] {
        assert_eq!(greater(std::hint::black_box(a), std::hint::black_box(b)), expected);
    }
    assert!(!greater(f128::NAN, 1.0));
    assert!(!greater(1.0, f128::NAN));
    assert!(greater(f128::INFINITY, f128::NEG_INFINITY));
}
