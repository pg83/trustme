// Counting and reordering bits on the wider integers went wrong in three ways:
// there was no 128-bit byte swap at all, a signed 128-bit value could not be
// handed to the 128-bit helpers, and a signed 64-bit one fell through to the
// 32-bit builtin. The conversion between the two 128-bit representations also
// exchanged the halves.
//
// Same shape as the upstream test intrinsics/intrinsics-integer.rs.
fn main() {
    // Leading zeros, at every width and both signednesses.
    assert_eq!(0u64.leading_zeros(), 64);
    assert_eq!(0i64.leading_zeros(), 64);
    assert_eq!(1u64.leading_zeros(), 63);
    assert_eq!(1i64.leading_zeros(), 63);
    assert_eq!(0usize.leading_zeros(), 64);
    assert_eq!(1isize.leading_zeros(), 63);
    assert_eq!(0u128.leading_zeros(), 128);
    assert_eq!(0i128.leading_zeros(), 128);
    assert_eq!(1u128.leading_zeros(), 127);
    assert_eq!(1i128.leading_zeros(), 127);

    // Trailing zeros.
    assert_eq!(0i64.trailing_zeros(), 64);
    assert_eq!(2i64.trailing_zeros(), 1);
    assert_eq!(0i128.trailing_zeros(), 128);
    assert_eq!((1i128 << 100).trailing_zeros(), 100);

    // Byte swap.
    assert_eq!(
        0x0102_0304_0506_0708_0910_1112_1314_1516u128.swap_bytes(),
        0x1615_1413_1211_1009_0807_0605_0403_0201u128
    );
    assert_eq!(1i128.swap_bytes(), 1i128 << 120);
    assert_eq!(1u64.swap_bytes(), 1u64 << 56);

    // Bit reversal.
    assert_eq!(1u128.reverse_bits(), 1u128 << 127);
    assert_eq!(1i128.reverse_bits(), i128::MIN);
    assert_eq!(1u64.reverse_bits(), 1u64 << 63);
}
