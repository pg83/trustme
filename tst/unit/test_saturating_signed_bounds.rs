// The saturating add/sub intrinsics clamp to the type's bounds, and the
// generated constant for the negative bound was missing a digit: `i32` clamped
// at -2^27 and `i64` at -2^59 instead of their minimums.
fn main() {
    assert_eq!(i32::MIN.saturating_add(-1), i32::MIN);
    assert_eq!(i32::MAX.saturating_add(1), i32::MAX);
    assert_eq!(i64::MIN.saturating_add(-1), i64::MIN);
    assert_eq!(i64::MAX.saturating_add(1), i64::MAX);
    assert_eq!(i128::MIN.saturating_add(-1), i128::MIN);
    assert_eq!(i128::MAX.saturating_add(1), i128::MAX);
    assert_eq!(i32::MIN.saturating_sub(1), i32::MIN);
    assert_eq!(i64::MIN.saturating_sub(1), i64::MIN);
    assert_eq!(i128::MIN.saturating_sub(1), i128::MIN);
    assert_eq!(isize::MIN.saturating_add(-1), isize::MIN);
    assert_eq!((-2isize).saturating_sub(isize::MAX), isize::MIN);
}
