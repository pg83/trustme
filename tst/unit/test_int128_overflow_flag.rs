// The carry out of a 128-bit addition cannot be read off the high word alone:
// adding the carry into it can leave it equal to what it went in as, and the
// check said "no overflow" then. Subtraction had the same hole. The values were
// right; only the flags were wrong, so `checked_add` handed back a `Some` that
// should have been `None`.
//
// Same shape as the library test coretests/intrinsics.rs
// (`carrying_mul_add`), which is built on these.

fn main() {
    // The case the old check missed: the high word comes out unchanged.
    assert_eq!(u128::MAX.overflowing_add(u128::MAX - 1), (u128::MAX - 2, true));
    assert_eq!((u128::MAX - 1).overflowing_add(u128::MAX), (u128::MAX - 2, true));
    assert_eq!(u128::MAX.checked_add(u128::MAX - 1), None);

    // And its subtraction counterpart.
    let a = u128::MAX << 64;
    assert_eq!(a.overflowing_sub(a + 1), (u128::MAX, true));
    assert_eq!(a.checked_sub(a + 1), None);

    // The ordinary cases still answer as before.
    assert_eq!(u128::MAX.overflowing_add(1), (0, true));
    assert_eq!(1u128.overflowing_add(2), (3, false));
    assert_eq!(5u128.overflowing_sub(3), (2, false));
    assert_eq!(0u128.overflowing_sub(1), (u128::MAX, true));
    assert_eq!(u128::MAX.wrapping_add(1), 0);

    // Multiplication accumulates the same carries.
    assert_eq!(u128::MAX.overflowing_mul(2), (u128::MAX - 1, true));
    assert_eq!((1u128 << 127).overflowing_mul(2), (0, true));
    assert_eq!(3u128.overflowing_mul(5), (15, false));
}
